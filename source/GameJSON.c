#include "GameJSON.h"
#include "wr/WRNumber.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRMath.h"
#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>


// Macros.
#define COLOR_CHANNEL_MAX (255)
#define COLOR_CHANNEL_COUNT (4U)
/** Bits per hex digit. */
#define HEX_DIGIT_BITS (4U)
/** 2^63 as a double: the exclusive upper bound of the int64 range. Exactly representable, so comparing
 *  against it (and against its negation, the inclusive lower bound) is exact. */
#define INT64_RANGE_LIMIT_AS_DOUBLE (9223372036854775808.0)
/** 2^64 as a double: the exclusive upper bound of the uint64 range, also exactly representable. */
#define UINT64_RANGE_LIMIT_AS_DOUBLE (18446744073709551616.0)
/** "#rrggbbaa": the longest hex color written. */
#define HEX_COLOR_MAX_BYTE_COUNT (9U)
/** The %g precisions tried for a float's shortest decimal: printf's default first, FLT_DECIMAL_DIG always reads back. */
#define SHORTEST_FLOAT_PRECISION_START 6
#define SHORTEST_FLOAT_TEXT_SIZE 32
/** Room for the decimal digits of any uint64 plus a terminator. */
#define UINT64_TEXT_SIZE 24
#define KEY_TINT ((const unsigned char*)u8"tint")
#define KEY_BRIGHTNESS ((const unsigned char*)u8"brightness")
#define KEY_OPACITY ((const unsigned char*)u8"opacity")


// Static functions.
/* Converts a JSON real number to an int64 only when it is a whole number inside the int64 range. Anything
   else (2.9, 1e19, inf, NaN) is rejected instead of being truncated or reaching the undefined behaviour of
   an out-of-range floating-to-integer cast. */
static Error RealToInt64(double value, int64_t* outValue)
{
    if (Math_IsNaNDouble(value) || Math_IsInfinityDouble(value) || (Math_TruncateDouble(value) != value)
        || (value < -INT64_RANGE_LIMIT_AS_DOUBLE) || (value >= INT64_RANGE_LIMIT_AS_DOUBLE))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Expected a whole number within the 64-bit integer range.");
    }

    *outValue = (int64_t)value;
    return Error_CreateSuccess();
}

/* The uint64 counterpart of RealToInt64: a whole number in [0, 2^64), nothing else. */
static Error RealToUInt64(double value, uint64_t* outValue)
{
    if (Math_IsNaNDouble(value) || Math_IsInfinityDouble(value) || (Math_TruncateDouble(value) != value)
        || (value < 0.0) || (value >= UINT64_RANGE_LIMIT_AS_DOUBLE))
    {
        return Error_Construct2(ErrorCode_InvalidJSON,
            "Expected a whole number within the unsigned 64-bit integer range.");
    }

    *outValue = (uint64_t)value;
    return Error_CreateSuccess();
}

static bool StringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool Equal = false;
    Error Result = StringUTF8_EqualsExact(a, b, &Equal);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false;
    }
    return Equal;
}

/* Re-raises a parse failure from WRNumber (which uses its own error codes) as ErrorCode_InvalidJSON, keeping its
   message, so every bad JSON value fails with the same code. */
static Error ToInvalidJSON(Error* inner, const char* expectation)
{
    Error Result = Error_Construct3(ErrorCode_InvalidJSON, u8"%s (%s)", expectation,
        (inner->Message != NULL) ? (const char*)inner->Message : "unparsable");
    Error_Deconstruct(inner);
    return Result;
}

/* A decimal that is finite as a double but does not fit a float would become infinite when stored. */
static bool IsFiniteFloatRange(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value) && (Math_AbsDouble(value) <= (double)FLT_MAX);
}

/* Stores the four channels (r, g, b, a) when each is within [0, 255]; a channel outside it is a content error, not
   clamped. */
static Error StoreChannels(const int64_t* channels, Color* outColor)
{
    for (size_t Index = 0; Index < COLOR_CHANNEL_COUNT; Index++)
    {
        if ((channels[Index] < 0) || (channels[Index] > COLOR_CHANNEL_MAX))
        {
            return Error_Construct3(ErrorCode_InvalidJSON, u8"Color channel %" PRId64 u8" is outside [0, %d].",
                channels[Index], COLOR_CHANNEL_MAX);
        }
    }
    outColor->r = (unsigned char)channels[0];
    outColor->g = (unsigned char)channels[1];
    outColor->b = (unsigned char)channels[2];
    outColor->a = (unsigned char)channels[3];
    return Error_CreateSuccess();
}

/* The value of a hex digit (0-9, a-f, A-F), or -1 for any other byte. */
static int GetHexDigitValue(unsigned char digit)
{
    if ((digit >= (unsigned char)'0') && (digit <= (unsigned char)'9'))
    {
        return (int)(digit - (unsigned char)'0');
    }
    if ((digit >= (unsigned char)'a') && (digit <= (unsigned char)'f'))
    {
        return (int)(digit - (unsigned char)'a') + 10;
    }
    if ((digit >= (unsigned char)'A') && (digit <= (unsigned char)'F'))
    {
        return (int)(digit - (unsigned char)'A') + 10;
    }
    return -1;
}

static Error ParseColorHexString(const unsigned char* text, Color* outColor)
{
    const unsigned char* Position = text;
    if (Position[0] == (unsigned char)'#')
    {
        Position++;
    }
    if ((Position[0] == (unsigned char)'0') && ((Position[1] == (unsigned char)'x') || (Position[1] == (unsigned char)'X')))
    {
        Position += 2;
    }

    size_t DigitCount = StringUTF8_GetByteLength(Position);
    if ((DigitCount != 6U) && (DigitCount != 8U))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Hex color must have 6 or 8 digits.");
    }

    // Digit by digit, so a sign, a space or another prefix is rejected rather than read as part of a number.
    uint32_t Value = 0U;
    for (size_t Index = 0; Index < DigitCount; Index++)
    {
        int Digit = GetHexDigitValue(Position[Index]);
        if (Digit < 0)
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "Hex color digits must be 0-9, a-f or A-F.");
        }
        Value = (Value << HEX_DIGIT_BITS) | (uint32_t)Digit;
    }

    if (DigitCount == 6U)
    {
        outColor->r = (unsigned char)((Value >> 16) & 0xFFU);
        outColor->g = (unsigned char)((Value >> 8) & 0xFFU);
        outColor->b = (unsigned char)(Value & 0xFFU);
        outColor->a = 255U;
    }
    else
    {
        outColor->r = (unsigned char)((Value >> 24) & 0xFFU);
        outColor->g = (unsigned char)((Value >> 16) & 0xFFU);
        outColor->b = (unsigned char)((Value >> 8) & 0xFFU);
        outColor->a = (unsigned char)(Value & 0xFFU);
    }
    return Error_CreateSuccess();
}

static Error ParseColorArray(JSONArray* array, Color* outColor)
{
    size_t Count = JSONArray_GetElementCount(array);
    if ((Count != 3U) && (Count != 4U))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Color array must have 3 or 4 elements.");
    }

    int64_t Channels[COLOR_CHANNEL_COUNT] = { 0, 0, 0, COLOR_CHANNEL_MAX };
    for (size_t Index = 0; Index < Count; Index++)
    {
        JSONObjectValue Element;
        Error GetResult = JSONArray_Get(array, Index, &Element);
        if (GetResult.Code != ErrorCode_Success)
        {
            return GetResult;
        }
        Error IntResult = GameJSON_ValueToInt64(&Element, &Channels[Index]);
        if (IntResult.Code != ErrorCode_Success)
        {
            return IntResult;
        }
    }
    return StoreChannels(Channels, outColor);
}

static Error ParseColorCompound(JSONCompound* compound, Color* outColor)
{
    int64_t Channels[COLOR_CHANNEL_COUNT] = { 0, 0, 0, COLOR_CHANNEL_MAX };
    const unsigned char* Keys[COLOR_CHANNEL_COUNT] = { (const unsigned char*)u8"r", (const unsigned char*)u8"g",
        (const unsigned char*)u8"b", (const unsigned char*)u8"a" };

    for (size_t Index = 0; Index < COLOR_CHANNEL_COUNT; Index++)
    {
        JSONObjectValue Value;
        bool Found = false;
        Error GetResult = JSONCompound_GetOptional(compound, Keys[Index], &Value, &Found);
        if (GetResult.Code != ErrorCode_Success)
        {
            return GetResult;
        }
        if (!Found)
        {
            if (Index < 3U)
            {
                return Error_Construct2(ErrorCode_InvalidJSON, "Color compound requires r, g and b.");
            }
            continue;
        }
        Error IntResult = GameJSON_ValueToInt64(&Value, &Channels[Index]);
        if (IntResult.Code != ErrorCode_Success)
        {
            return IntResult;
        }
    }
    return StoreChannels(Channels, outColor);
}

// Reports whether a compound carries any render-color-specific key (tint/brightness/opacity).
static Error IsRenderColorCompound(JSONCompound* compound, bool* outResult)
{
    const unsigned char* Keys[3] = { KEY_TINT, KEY_BRIGHTNESS, KEY_OPACITY };
    *outResult = false;
    for (size_t Index = 0; Index < 3U; Index++)
    {
        JSONObjectValue Value;
        bool Found = false;
        Error GetResult = JSONCompound_GetOptional(compound, Keys[Index], &Value, &Found);
        if (GetResult.Code != ErrorCode_Success)
        {
            return GetResult;
        }
        if (Found)
        {
            *outResult = true;
            return Error_CreateSuccess();
        }
    }
    return Error_CreateSuccess();
}

/* Reads one vector component as a finite double: element @p index of an array, or the key @p name of a compound. */
static Error ReadVectorComponent(const JSONObjectValue* value, size_t index, const unsigned char* name,
    double* outComponent)
{
    JSONObjectValue Component;
    if (value->Type == JSONValueType_Array)
    {
        Error GetResult = JSONArray_Get(value->Value.Array, index, &Component);
        if (GetResult.Code != ErrorCode_Success)
        {
            return GetResult;
        }
    }
    else
    {
        Error GetResult = JSONCompound_Get(value->Value.Compound, name, &Component);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            return Error_Construct3(ErrorCode_InvalidJSON, u8"Vector compound requires \"%s\".", (const char*)name);
        }
    }

    return GameJSON_ValueToDouble(&Component, outComponent);
}

// Borrows a pool string holding the given bytes (without a terminator, like every WRJSON string).
static Error CreateStringValue(JSONObjectPool* pool, const unsigned char* bytes, size_t byteCount, JSONObjectValue* outValue)
{
    GenericBuffer* Buffer = NULL;
    Error Result = JSONObjectPool_BorrowString(pool, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // AppendRangeBytes only reads its source; the API is just not declared const.
    if ((byteCount > 0U) && !GenericBuffer_AppendRangeBytes(Buffer, (unsigned char*)bytes, byteCount))
    {
        Error ReturnResult = JSONObjectPool_ReturnString(pool, Buffer);
        Error_Deconstruct(&ReturnResult);
        return Error_Construct2(ErrorCode_BufferTooLarge, "GameJSON: could not build a string value.");
    }
    *outValue = JSONObjectValue_CreateString(Buffer);
    return Error_CreateSuccess();
}

/* Builds an array of floats and hands it out, or returns it to the pool on failure. */
static Error CreateFloatArrayValue(JSONObjectPool* pool, const float* components, size_t count, JSONObjectValue* outValue)
{
    if ((pool == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameJSON: pool and outValue must not be NULL.");
    }

    JSONArray* Array = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    for (size_t Index = 0; (Index < count) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Element = GameJSON_CreateFloatValue(components[Index]);
        Result = JSONArray_Add(Array, &Element);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    *outValue = JSONObjectValue_CreateArray(Array);
    return Error_CreateSuccess();
}

/* Fills the compound form of a render color: { "tint": [r, g, b], "brightness": x, "opacity": y }. */
static Error FillRenderColorCompound(JSONObjectPool* pool, JSONCompound* compound, RenderColor color)
{
    JSONArray* Tint = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &Tint);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const unsigned char Channels[3] = { color.Tint.r, color.Tint.g, color.Tint.b };
    for (size_t Index = 0; (Index < 3U) && (Result.Code == ErrorCode_Success); Index++)
    {
        JSONObjectValue Channel = JSONObjectValue_CreateInteger((int64_t)Channels[Index]);
        Result = JSONArray_Add(Tint, &Channel);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnArray(pool, Tint, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    JSONObjectValue TintValue = JSONObjectValue_CreateArray(Tint);
    Result = JSONCompound_Set(compound, KEY_TINT, &TintValue);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnArray(pool, Tint, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }

    JSONObjectValue Brightness = GameJSON_CreateFloatValue(color.Brightness);
    Result = JSONCompound_Set(compound, KEY_BRIGHTNESS, &Brightness);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    JSONObjectValue Opacity = GameJSON_CreateFloatValue(color.Opacity);
    return JSONCompound_Set(compound, KEY_OPACITY, &Opacity);
}


// Public functions.
Error GameJSON_ValueToInt64(const JSONObjectValue* value, int64_t* outValue)
{
    if (value->Type == JSONValueType_Integer)
    {
        *outValue = value->Value.Integer;
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_RealNumber)
    {
        return RealToInt64(value->Value.RealNumber, outValue);
    }
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error ConvertResult = GameJSON_ValueToOwnedString(value, &Text);
        if (ConvertResult.Code != ErrorCode_Success)
        {
            return ConvertResult;
        }
        int64_t Parsed = 0;
        Error ParseResult = Number_Int64FromString(Text, NUMBER_BASE_AUTO_DETECT, &Parsed);
        Memory_Free(Text);
        if (ParseResult.Code != ErrorCode_Success)
        {
            return ToInvalidJSON(&ParseResult, "Expected an integer value");
        }
        *outValue = Parsed;
        return Error_CreateSuccess();
    }
    return Error_Construct2(ErrorCode_InvalidJSON, "Expected an integer value.");
}

Error GameJSON_ValueToUInt64(const JSONObjectValue* value, uint64_t* outValue)
{
    if (value->Type == JSONValueType_Integer)
    {
        if (value->Value.Integer < 0)
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "Expected a non-negative integer value.");
        }
        *outValue = (uint64_t)value->Value.Integer;
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_RealNumber)
    {
        return RealToUInt64(value->Value.RealNumber, outValue);
    }
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error ConvertResult = GameJSON_ValueToOwnedString(value, &Text);
        if (ConvertResult.Code != ErrorCode_Success)
        {
            return ConvertResult;
        }
        uint64_t Parsed = 0U;
        Error ParseResult = Number_UInt64FromString(Text, NUMBER_BASE_AUTO_DETECT, &Parsed);
        Memory_Free(Text);
        if (ParseResult.Code != ErrorCode_Success)
        {
            return ToInvalidJSON(&ParseResult, "Expected a non-negative integer value");
        }
        *outValue = Parsed;
        return Error_CreateSuccess();
    }
    return Error_Construct2(ErrorCode_InvalidJSON, "Expected a non-negative integer value.");
}

Error GameJSON_ValueToDouble(const JSONObjectValue* value, double* outValue)
{
    if (value->Type == JSONValueType_RealNumber)
    {
        if (Math_IsNaNDouble(value->Value.RealNumber) || Math_IsInfinityDouble(value->Value.RealNumber))
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "Expected a finite decimal value.");
        }
        *outValue = value->Value.RealNumber;
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_Integer)
    {
        *outValue = (double)value->Value.Integer;
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error ConvertResult = GameJSON_ValueToOwnedString(value, &Text);
        if (ConvertResult.Code != ErrorCode_Success)
        {
            return ConvertResult;
        }
        double Parsed = 0.0;
        Error ParseResult = Number_DoubleFromString(Text, &Parsed, DecimalSeparator_Period);
        Memory_Free(Text);
        if (ParseResult.Code != ErrorCode_Success)
        {
            return ToInvalidJSON(&ParseResult, "Expected a decimal value");
        }
        if (Math_IsNaNDouble(Parsed) || Math_IsInfinityDouble(Parsed))
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "Expected a finite decimal value.");
        }
        *outValue = Parsed;
        return Error_CreateSuccess();
    }
    return Error_Construct2(ErrorCode_InvalidJSON, "Expected a decimal value.");
}

Error GameJSON_ValueToBoolean(const JSONObjectValue* value, bool* outValue)
{
    if (value->Type == JSONValueType_Boolean)
    {
        *outValue = value->Value.Boolean;
        return Error_CreateSuccess();
    }
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error TextResult = GameJSON_ValueToOwnedString(value, &Text);
        if (TextResult.Code != ErrorCode_Success)
        {
            return TextResult;
        }
        Error Result = Error_CreateSuccess();
        if (StringsEqual(Text, (const unsigned char*)u8"true"))
        {
            *outValue = true;
        }
        else if (StringsEqual(Text, (const unsigned char*)u8"false"))
        {
            *outValue = false;
        }
        else
        {
            Result = Error_Construct2(ErrorCode_InvalidJSON, "Boolean must be true or false.");
        }
        Memory_Free(Text);
        return Result;
    }
    return Error_Construct2(ErrorCode_InvalidJSON, "Expected a boolean value.");
}

Error GameJSON_ValueToOwnedString(const JSONObjectValue* value, unsigned char** outString)
{
    if (value->Type != JSONValueType_String)
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Expected a string value.");
    }

    GenericBuffer* Buffer = value->Value.String;
    size_t Length = (Buffer != NULL) ? Buffer->_count : 0U;
    unsigned char* Copy = Memory_Allocate(Length + 1U);
    if (Length > 0U)
    {
        Memory_Copy(Buffer->_data, Copy, Length);
    }
    Copy[Length] = (unsigned char)0;
    *outString = Copy;
    return Error_CreateSuccess();
}

Error GameJSON_ParseColor(const JSONObjectValue* value, Color* outColor)
{
    if (value->Type == JSONValueType_String)
    {
        unsigned char* Text = NULL;
        Error TextResult = GameJSON_ValueToOwnedString(value, &Text);
        if (TextResult.Code != ErrorCode_Success)
        {
            return TextResult;
        }
        Error Result = ParseColorHexString(Text, outColor);
        Memory_Free(Text);
        return Result;
    }
    if (value->Type == JSONValueType_Array)
    {
        return ParseColorArray(value->Value.Array, outColor);
    }
    if (value->Type == JSONValueType_Compound)
    {
        return ParseColorCompound(value->Value.Compound, outColor);
    }
    return Error_Construct2(ErrorCode_InvalidJSON, "A color must be a string, array or compound.");
}

Error GameJSON_ParseRenderColor(const JSONObjectValue* value, RenderColor* outColor)
{
    // A compound carrying tint/brightness/opacity is the explicit render-color form; anything else is a
    // plain color whose RGB is the tint, alpha the opacity, and brightness 1.0.
    if (value->Type == JSONValueType_Compound)
    {
        bool IsRenderColor = false;
        Error CheckResult = IsRenderColorCompound(value->Value.Compound, &IsRenderColor);
        if (CheckResult.Code != ErrorCode_Success)
        {
            return CheckResult;
        }

        if (IsRenderColor)
        {
            JSONCompound* Compound = value->Value.Compound;
            Color Tint = { .r = 255U, .g = 255U, .b = 255U, .a = 255U };

            JSONObjectValue TintValue;
            bool HasTint = false;
            Error TintResult = JSONCompound_GetOptional(Compound, (const unsigned char*)u8"tint", &TintValue, &HasTint);
            if (TintResult.Code != ErrorCode_Success)
            {
                return TintResult;
            }
            if (HasTint)
            {
                Error ColorResult = GameJSON_ParseColor(&TintValue, &Tint);
                if (ColorResult.Code != ErrorCode_Success)
                {
                    return ColorResult;
                }
            }

            double Brightness = 1.0;
            Error BrightnessResult = GameJSON_ReadOptionalDouble(Compound, (const unsigned char*)u8"brightness", 1.0, &Brightness);
            if (BrightnessResult.Code != ErrorCode_Success)
            {
                return BrightnessResult;
            }

            double Opacity = (double)Tint.a / (double)COLOR_CHANNEL_MAX;
            Error OpacityResult = GameJSON_ReadOptionalDouble(Compound, (const unsigned char*)u8"opacity", Opacity, &Opacity);
            if (OpacityResult.Code != ErrorCode_Success)
            {
                return OpacityResult;
            }

            if ((Brightness < (double)RENDER_COLOR_BRIGHTNESS_MIN) || (Brightness > (double)RENDER_COLOR_BRIGHTNESS_MAX)
                || (Opacity < (double)RENDER_COLOR_OPACITY_MIN) || (Opacity > (double)RENDER_COLOR_OPACITY_MAX))
            {
                return Error_Construct2(ErrorCode_InvalidJSON, "Render color brightness and opacity must be within [0, 1].");
            }

            outColor->Tint = Tint;
            outColor->Brightness = (float)Brightness;
            outColor->Opacity = (float)Opacity;
            return Error_CreateSuccess();
        }
    }

    Color PlainColor;
    Error ColorResult = GameJSON_ParseColor(value, &PlainColor);
    if (ColorResult.Code != ErrorCode_Success)
    {
        return ColorResult;
    }
    outColor->Tint = PlainColor;
    outColor->Brightness = 1.0f;
    outColor->Opacity = (float)PlainColor.a / (float)COLOR_CHANNEL_MAX;
    return Error_CreateSuccess();
}

Error GameJSON_ParseVector2(const JSONObjectValue* value, Vector2* outVector)
{
    if (value->Type == JSONValueType_Compound)
    {
        JSONObjectValue Component;
        Error XResult = JSONCompound_Get(value->Value.Compound, (const unsigned char*)u8"x", &Component);
        if (XResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&XResult);
            return Error_Construct2(ErrorCode_InvalidJSON, "Vector compound requires \"x\".");
        }
        double X = 0.0;
        Error XConvert = GameJSON_ValueToDouble(&Component, &X);
        if (XConvert.Code != ErrorCode_Success)
        {
            return XConvert;
        }

        Error YResult = JSONCompound_Get(value->Value.Compound, (const unsigned char*)u8"y", &Component);
        if (YResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&YResult);
            return Error_Construct2(ErrorCode_InvalidJSON, "Vector compound requires \"y\".");
        }
        double Y = 0.0;
        Error YConvert = GameJSON_ValueToDouble(&Component, &Y);
        if (YConvert.Code != ErrorCode_Success)
        {
            return YConvert;
        }

        if (!IsFiniteFloatRange(X) || !IsFiniteFloatRange(Y))
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "Vector components must be finite decimals.");
        }
        outVector->x = (float)X;
        outVector->y = (float)Y;
        return Error_CreateSuccess();
    }

    if (value->Type == JSONValueType_Array)
    {
        JSONArray* Array = value->Value.Array;
        if (JSONArray_GetElementCount(Array) < 2U)
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "Vector array requires two elements.");
        }
        JSONObjectValue Element;
        Error XResult = JSONArray_Get(Array, 0U, &Element);
        if (XResult.Code != ErrorCode_Success)
        {
            return XResult;
        }
        double X = 0.0;
        Error XConvert = GameJSON_ValueToDouble(&Element, &X);
        if (XConvert.Code != ErrorCode_Success)
        {
            return XConvert;
        }

        Error YResult = JSONArray_Get(Array, 1U, &Element);
        if (YResult.Code != ErrorCode_Success)
        {
            return YResult;
        }
        double Y = 0.0;
        Error YConvert = GameJSON_ValueToDouble(&Element, &Y);
        if (YConvert.Code != ErrorCode_Success)
        {
            return YConvert;
        }

        if (!IsFiniteFloatRange(X) || !IsFiniteFloatRange(Y))
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "Vector components must be finite decimals.");
        }
        outVector->x = (float)X;
        outVector->y = (float)Y;
        return Error_CreateSuccess();
    }

    return Error_Construct2(ErrorCode_InvalidJSON, "Expected a vector.");
}

Error GameJSON_ParseVector3Doubles(const JSONObjectValue* value, double* outComponents)
{
    if ((value->Type == JSONValueType_Array) && (JSONArray_GetElementCount(value->Value.Array) != 3U))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Vector array requires exactly three elements.");
    }
    if ((value->Type != JSONValueType_Array) && (value->Type != JSONValueType_Compound))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Expected a vector.");
    }

    const unsigned char* Names[3] = { (const unsigned char*)u8"x", (const unsigned char*)u8"y",
        (const unsigned char*)u8"z" };
    double Components[3] = { 0.0, 0.0, 0.0 };
    for (size_t Index = 0; Index < 3U; Index++)
    {
        Error Result = ReadVectorComponent(value, Index, Names[Index], &Components[Index]);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Memory_Copy(Components, outComponents, sizeof(Components));
    return Error_CreateSuccess();
}

Error GameJSON_ParseVector3Double(const JSONObjectValue* value, Vector3Double* outVector)
{
    double Components[3] = { 0.0, 0.0, 0.0 };
    Error Result = GameJSON_ParseVector3Doubles(value, Components);
    if (Result.Code == ErrorCode_Success)
    {
        *outVector = Vector3Double_Create(Components[0], Components[1], Components[2]);
    }
    return Result;
}

Error GameJSON_ParseVector3(const JSONObjectValue* value, Vector3* outVector)
{
    double Components[3] = { 0.0, 0.0, 0.0 };
    Error Result = GameJSON_ParseVector3Doubles(value, Components);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsFiniteFloatRange(Components[0]) || !IsFiniteFloatRange(Components[1]) || !IsFiniteFloatRange(Components[2]))
    {
        return Error_Construct2(ErrorCode_InvalidJSON, "Vector components must be finite decimals.");
    }
    *outVector = (Vector3){ .x = (float)Components[0], .y = (float)Components[1], .z = (float)Components[2] };
    return Error_CreateSuccess();
}

Error GameJSON_ParseVector2Int(const JSONObjectValue* value, int64_t* outX, int64_t* outY)
{
    if (value->Type == JSONValueType_Compound)
    {
        JSONObjectValue Component;
        Error XResult = JSONCompound_Get(value->Value.Compound, (const unsigned char*)u8"x", &Component);
        if (XResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&XResult);
            return Error_Construct2(ErrorCode_InvalidJSON, "Vector compound requires \"x\".");
        }
        Error XConvert = GameJSON_ValueToInt64(&Component, outX);
        if (XConvert.Code != ErrorCode_Success)
        {
            return XConvert;
        }

        Error YResult = JSONCompound_Get(value->Value.Compound, (const unsigned char*)u8"y", &Component);
        if (YResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&YResult);
            return Error_Construct2(ErrorCode_InvalidJSON, "Vector compound requires \"y\".");
        }
        return GameJSON_ValueToInt64(&Component, outY);
    }

    if (value->Type == JSONValueType_Array)
    {
        JSONArray* Array = value->Value.Array;
        if (JSONArray_GetElementCount(Array) < 2U)
        {
            return Error_Construct2(ErrorCode_InvalidJSON, "Vector array requires two elements.");
        }
        JSONObjectValue Element;
        Error XResult = JSONArray_Get(Array, 0U, &Element);
        if (XResult.Code != ErrorCode_Success)
        {
            return XResult;
        }
        Error XConvert = GameJSON_ValueToInt64(&Element, outX);
        if (XConvert.Code != ErrorCode_Success)
        {
            return XConvert;
        }

        Error YResult = JSONArray_Get(Array, 1U, &Element);
        if (YResult.Code != ErrorCode_Success)
        {
            return YResult;
        }
        return GameJSON_ValueToInt64(&Element, outY);
    }

    return Error_Construct2(ErrorCode_InvalidJSON, "Expected a vector.");
}

Error GameJSON_ReadOptionalInteger(JSONCompound* compound, const unsigned char* key,
    int64_t defaultValue, int64_t* outValue)
{
    *outValue = defaultValue;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success)
    {
        return GetResult;
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    return GameJSON_ValueToInt64(&Value, outValue);
}

Error GameJSON_ReadOptionalDouble(JSONCompound* compound, const unsigned char* key,
    double defaultValue, double* outValue)
{
    *outValue = defaultValue;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success)
    {
        return GetResult;
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    return GameJSON_ValueToDouble(&Value, outValue);
}

Error GameJSON_ReadOptionalBoolean(JSONCompound* compound, const unsigned char* key,
    bool defaultValue, bool* outValue)
{
    *outValue = defaultValue;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success)
    {
        return GetResult;
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    return GameJSON_ValueToBoolean(&Value, outValue);
}

Error GameJSON_ReadOptionalVector2Int(JSONCompound* compound, const unsigned char* key,
    int64_t defaultX, int64_t defaultY, int64_t* outX, int64_t* outY)
{
    *outX = defaultX;
    *outY = defaultY;
    JSONObjectValue Value;
    bool Found = false;
    Error GetResult = JSONCompound_GetOptional(compound, key, &Value, &Found);
    if (GetResult.Code != ErrorCode_Success)
    {
        return GetResult;
    }
    if (!Found)
    {
        return Error_CreateSuccess();
    }
    return GameJSON_ParseVector2Int(&Value, outX, outY);
}

JSONObjectValue GameJSON_CreateFloatValue(float value)
{
    char Text[SHORTEST_FLOAT_TEXT_SIZE];
    for (int Precision = SHORTEST_FLOAT_PRECISION_START; Precision <= FLT_DECIMAL_DIG; Precision++)
    {
        int Count = snprintf(Text, sizeof(Text), "%.*g", Precision, (double)value);
        if ((Count < 0) || ((size_t)Count >= sizeof(Text)))
        {
            break;
        }
        double ReadBack = strtod(Text, NULL);
        if ((Math_AbsDouble(ReadBack) <= (double)FLT_MAX) && ((float)ReadBack == value))
        {
            return JSONObjectValue_CreateRealNumber(ReadBack);
        }
    }
    return JSONObjectValue_CreateRealNumber((double)value);
}

Error GameJSON_CreateStringValue(JSONObjectPool* pool, const unsigned char* text, JSONObjectValue* outValue)
{
    if ((pool == NULL) || (text == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameJSON_CreateStringValue: pool, text and outValue must not be NULL.");
    }
    return CreateStringValue(pool, text, StringUTF8_GetByteLength(text), outValue);
}

Error GameJSON_CreateUInt64Value(JSONObjectPool* pool, uint64_t value, JSONObjectValue* outValue)
{
    if ((pool == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameJSON_CreateUInt64Value: pool and outValue must not be NULL.");
    }
    if (value <= (uint64_t)INT64_MAX)
    {
        *outValue = JSONObjectValue_CreateInteger((int64_t)value);
        return Error_CreateSuccess();
    }

    char Text[UINT64_TEXT_SIZE];
    int Count = snprintf(Text, sizeof(Text), "%" PRIu64, value);
    if ((Count <= 0) || ((size_t)Count >= sizeof(Text)))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "GameJSON_CreateUInt64Value: could not format the number.");
    }
    return CreateStringValue(pool, (const unsigned char*)Text, (size_t)Count, outValue);
}

Error GameJSON_CreateColorValue(JSONObjectPool* pool, Color color, JSONObjectValue* outValue)
{
    if ((pool == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameJSON_CreateColorValue: pool and outValue must not be NULL.");
    }

    static const unsigned char HexDigits[] = u8"0123456789abcdef";
    const unsigned char Channels[COLOR_CHANNEL_COUNT] = { color.r, color.g, color.b, color.a };
    size_t ChannelCount = (color.a == (unsigned char)COLOR_CHANNEL_MAX) ? 3U : COLOR_CHANNEL_COUNT;

    unsigned char Hex[HEX_COLOR_MAX_BYTE_COUNT];
    Hex[0] = (unsigned char)'#';
    for (size_t Index = 0; Index < ChannelCount; Index++)
    {
        Hex[1U + (2U * Index)] = HexDigits[Channels[Index] >> HEX_DIGIT_BITS];
        Hex[2U + (2U * Index)] = HexDigits[Channels[Index] & 0x0FU];
    }
    return CreateStringValue(pool, Hex, 1U + (2U * ChannelCount), outValue);
}

Error GameJSON_CreateRenderColorValue(JSONObjectPool* pool, RenderColor color, JSONObjectValue* outValue)
{
    if ((pool == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "GameJSON_CreateRenderColorValue: pool and outValue must not be NULL.");
    }
    if ((color.Brightness == 1.0f) && (color.Opacity == 1.0f))
    {
        Color Opaque = color.Tint;
        Opaque.a = (unsigned char)COLOR_CHANNEL_MAX;
        return GameJSON_CreateColorValue(pool, Opaque, outValue);
    }

    JSONCompound* Compound = NULL;
    Error Result = JSONObjectPool_BorrowCompound(pool, &Compound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = FillRenderColorCompound(pool, Compound, color);
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnCompound(pool, Compound, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    *outValue = JSONObjectValue_CreateCompound(Compound);
    return Error_CreateSuccess();
}

Error GameJSON_CreateVector2Value(JSONObjectPool* pool, Vector2 vector, JSONObjectValue* outValue)
{
    const float Components[2] = { vector.x, vector.y };
    return CreateFloatArrayValue(pool, Components, 2U, outValue);
}

Error GameJSON_CreateVector3Value(JSONObjectPool* pool, Vector3 vector, JSONObjectValue* outValue)
{
    const float Components[3] = { vector.x, vector.y, vector.z };
    return CreateFloatArrayValue(pool, Components, 3U, outValue);
}

Error GameJSON_CreateVector3DoubleValue(JSONObjectPool* pool, Vector3Double vector, JSONObjectValue* outValue)
{
    if ((pool == NULL) || (outValue == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameJSON: pool and outValue must not be NULL.");
    }

    JSONArray* Array = NULL;
    Error Result = JSONObjectPool_BorrowArray(pool, &Array);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const double Components[3] = { vector.X, vector.Y, vector.Z };
    for (size_t Index = 0; (Index < 3U) && (Result.Code == ErrorCode_Success); Index++)
    {
        // JSON_Serialize writes a real number as the shortest text that reads back to the same double.
        JSONObjectValue Element = JSONObjectValue_CreateRealNumber(Components[Index]);
        Result = JSONArray_Add(Array, &Element);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Error ReturnResult = JSONObjectPool_ReturnArray(pool, Array, true);
        Error_Deconstruct(&ReturnResult);
        return Result;
    }
    *outValue = JSONObjectValue_CreateArray(Array);
    return Error_CreateSuccess();
}
