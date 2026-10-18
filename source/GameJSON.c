#include "GameJSON.h"
#include "wr/WRNumber.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRMath.h"
#include <float.h>
#include <inttypes.h>


// Macros.
#define COLOR_CHANNEL_MAX (255)
#define COLOR_CHANNEL_COUNT (4U)
/** Bits per hex digit. */
#define HEX_DIGIT_BITS (4U)
/** 2^63 as a double: the exclusive upper bound of the int64 range. Exactly representable, so comparing
 *  against it (and against its negation, the inclusive lower bound) is exact. */
#define INT64_RANGE_LIMIT_AS_DOUBLE (9223372036854775808.0)


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
    const unsigned char* Keys[3] = { (const unsigned char*)u8"tint", (const unsigned char*)u8"brightness",
        (const unsigned char*)u8"opacity" };
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
