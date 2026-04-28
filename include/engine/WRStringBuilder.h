#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "WRUnicode.h"
#include "WRNumber.h"


typedef struct StringBuilderStruct
{
    /* Code outside this module should NOT touch the buffers directly. */
    GenericBuffer _selfString;
    GenericBuffer _selfTempBuffer;
    GenericBuffer* _activeStringBuffer;
    GenericBuffer* _activeTempBuffer;
    bool _areBuffersOwned;
} StringBuilder;


// Functions.
Error StringBuilder_Construct1(StringBuilder* self, size_t initialCapacity);

Error StringBuilder_Construct2(StringBuilder* self, GenericBuffer* stringBuffer, GenericBuffer* tempBuffer);

Error StringBuilder_Deconstruct(StringBuilder* self);



Error StringBuilder_InsertStringFromBuilder(StringBuilder* self, size_t index, StringBuilder* other);

Error StringBuilder_InsertString(StringBuilder* self, size_t index, const unsigned char* str);

Error StringBuilder_InsertSubstring(StringBuilder* self, size_t index, const unsigned char* str, size_t length);

/* UTF-8 string representation of the code point. */
Error StringBuilder_InsertCodePoint(StringBuilder* self, size_t index, CodePoint codePoint);

Error StringBuilder_InsertInt8(StringBuilder* self, size_t index, int8_t value, int32_t base, bool includePrefix);

Error StringBuilder_InsertUInt8(StringBuilder* self, size_t index, uint8_t value, int32_t base, bool includePrefix);

Error StringBuilder_InsertInt16(StringBuilder* self, size_t index, int16_t value, int32_t base, bool includePrefix);

Error StringBuilder_InsertUInt16(StringBuilder* self, size_t index, uint16_t value, int32_t base, bool includePrefix);

Error StringBuilder_InsertInt32(StringBuilder* self, size_t index, int32_t value, int32_t base, bool includePrefix);

Error StringBuilder_InsertUInt32(StringBuilder* self, size_t index, uint32_t value, int32_t base, bool includePrefix);

Error StringBuilder_InsertInt64(StringBuilder* self, size_t index, int64_t value, int32_t base, bool includePrefix);

Error StringBuilder_InsertUInt64(StringBuilder* self, size_t index, uint64_t value, int32_t base, bool includePrefix);

Error StringBuilder_InsertFloat(StringBuilder* self, size_t index, float value, DecimalFormatOptions options);

Error StringBuilder_InsertDouble(StringBuilder* self, size_t index, double value, DecimalFormatOptions options);

/* "true" or "false" */
Error StringBuilder_InsertBoolean(StringBuilder* self, size_t index, bool value);

Error StringBuilder_RemoveRange(StringBuilder* self, size_t startIndex, size_t length);

Error StringBuilder_Truncate(StringBuilder* self, size_t newLength);

static inline size_t StringBuilder_GetLength(StringBuilder* self)
{
    return self->_activeStringBuffer->_count;
}

Error StringBuilder_EnsureTotalCapacity(StringBuilder* self, size_t requiredTotalCapacity);

Error StringBuilder_ReserveMoreCapacity(StringBuilder* self, size_t addedCapacity);

/* This string is read-only. */
static inline const unsigned char* StringBuilder_GetStringBacked(StringBuilder* self)
{
    return self->_activeStringBuffer->_data;
}

Error StringBuilder_CopyTo(StringBuilder* self, GenericBuffer* destination);



static inline Error StringBuilder_AppendStringFromBuilder(StringBuilder* self, StringBuilder* other)
{
    return StringBuilder_InsertStringFromBuilder(self, StringBuilder_GetLength(self), other);
}

static inline Error StringBuilder_AppendString(StringBuilder* self, const unsigned char* str)
{
    return StringBuilder_InsertString(self, StringBuilder_GetLength(self), str);
}

static inline Error StringBuilder_AppendSubstring(StringBuilder* self, const unsigned char* str, size_t length)
{
    return StringBuilder_InsertSubstring(self, StringBuilder_GetLength(self), str, length);
}

static inline Error StringBuilder_AppendCodePoint(StringBuilder* self, CodePoint codePoint)
{
    return StringBuilder_InsertCodePoint(self, StringBuilder_GetLength(self), codePoint);
}

static inline Error StringBuilder_AppendInt8(StringBuilder* self, int8_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertInt8(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

static inline Error StringBuilder_AppendUInt8(StringBuilder* self, uint8_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertUInt8(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

static inline Error StringBuilder_AppendInt16(StringBuilder* self, int16_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertInt16(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

static inline Error StringBuilder_AppendUInt16(StringBuilder* self, uint16_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertUInt16(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

static inline Error StringBuilder_AppendInt32(StringBuilder* self, int32_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertInt32(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

static inline Error StringBuilder_AppendUInt32(StringBuilder* self, uint32_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertUInt32(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

static inline Error StringBuilder_AppendInt64(StringBuilder* self, int64_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertInt64(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

static inline Error StringBuilder_AppendUInt64(StringBuilder* self, uint64_t value, int32_t base, bool includePrefix)
{
    return StringBuilder_InsertUInt64(self, StringBuilder_GetLength(self), value, base, includePrefix);
}

static inline Error StringBuilder_AppendFloat(StringBuilder* self, float value, DecimalFormatOptions options)
{
    return StringBuilder_InsertFloat(self, StringBuilder_GetLength(self), value, options);
}

static inline Error StringBuilder_AppendDouble(StringBuilder* self, double value, DecimalFormatOptions options)
{
    return StringBuilder_InsertDouble(self, StringBuilder_GetLength(self), value, options);
}

static inline Error StringBuilder_AppendBoolean(StringBuilder* self, bool value)
{
    return StringBuilder_InsertBoolean(self, StringBuilder_GetLength(self), value);
}
