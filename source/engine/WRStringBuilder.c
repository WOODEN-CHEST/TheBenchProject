#include "WRStringBuilder.h"
#include "WRChar.h"
#include "WRString.h"


// Macros.
#define STRING_BUILDER_BYTE_ELEMENT_SIZE (sizeof(unsigned char))


// Types.
typedef struct IntegerInsertContextStruct
{
    union
    {
        int8_t Int8;
        uint8_t UInt8;
        int16_t Int16;
        uint16_t UInt16;
        int32_t Int32;
        uint32_t UInt32;
        int64_t Int64;
        uint64_t UInt64;
        float Float;
        double Double;
    } Value;
    int32_t Base;
    bool IncludePrefix;
    DecimalFormatOptions Options;
} IntegerInsertContext;


// Fields.
static const unsigned char* const STRING_BUILDER_TRUE = u8"true";
static const unsigned char* const STRING_BUILDER_FALSE = u8"false";


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"StringBuilder argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateInvalidStateError(void)
{
    return Error_Construct1(ErrorCode_InvalidState,
        u8"The StringBuilder is not initialized.");
}

static Error CreateByteBufferTypeError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"StringBuilder argument \"%s\" must be a byte buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreateReadOnlyBufferError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"StringBuilder argument \"%s\" must be writable.",
        argumentName);
}

static Error CreateInvalidEncodingError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidTextEncoding,
        u8"Cannot %s because the string does not contain valid UTF-8.",
        operationName);
}

static Error CreateInvalidCodePointError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidCodePoint,
        u8"Cannot %s because the code point is not valid UTF-8.",
        operationName);
}

static Error CreateBufferTooSmallError(const unsigned char* operationName, size_t requiredCapacity)
{
    return Error_Construct3(ErrorCode_BufferTooSmall,
        u8"Cannot %s because the buffer requires at least %zu elements of capacity.",
        operationName,
        requiredCapacity);
}

static Error CreateIndexOutOfBoundsError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_IndexOutOfBounds,
        u8"Cannot %s because the given string index is outside the valid range.",
        operationName);
}

static Error CreateOverflowError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Cannot %s because the required size exceeds the supported range.",
        operationName);
}

static bool StringBuilderBufferAllocate(GenericBuffer* destination, size_t requestedCapacity)
{
    void* NewData = Memory_Reallocate(destination->_data, requestedCapacity * destination->_elementSize);

    if (NewData == NULL)
    {
        return false;
    }

    destination->_data = NewData;
    destination->_capacity = requestedCapacity;
    return true;
}

static size_t GetByteLength(const unsigned char* str)
{
    size_t Length = 0;

    if (str == NULL)
    {
        return 0;
    }

    while (str[Length] != u8'\0')
    {
        Length++;
    }

    return Length;
}

static bool TryReadCodePointAt(const unsigned char* str,
    size_t byteLength,
    size_t index,
    size_t* outByteCount)
{
    size_t ByteCount = 0;

    if ((str == NULL) || (index >= byteLength))
    {
        return false;
    }

    ByteCount = CharUTF8_GetByteCountChar(str + index);
    if ((ByteCount == 0) || ((index + ByteCount) > byteLength))
    {
        return false;
    }
    if (!CharUTF8_IsCharBufferValid(str + index, byteLength - index))
    {
        return false;
    }

    if (outByteCount != NULL)
    {
        *outByteCount = ByteCount;
    }

    return true;
}

static bool IsCharacterBoundary(const unsigned char* str, size_t byteLength, size_t index)
{
    size_t CurrentIndex = 0;

    if (index > byteLength)
    {
        return false;
    }
    if (index == byteLength)
    {
        return true;
    }

    while (CurrentIndex < byteLength)
    {
        size_t ByteCount = 0;

        if (!TryReadCodePointAt(str, byteLength, CurrentIndex, &ByteCount))
        {
            return false;
        }
        if (CurrentIndex == index)
        {
            return true;
        }

        CurrentIndex += ByteCount;
    }

    return false;
}

static Error ValidateUTF8Region(const unsigned char* str, size_t byteLength, const unsigned char* operationName)
{
    size_t CurrentIndex = 0;

    if ((str == NULL) && (byteLength > 0))
    {
        return CreateNullArgumentError(u8"str");
    }

    while (CurrentIndex < byteLength)
    {
        size_t ByteCount = 0;

        if (!TryReadCodePointAt(str, byteLength, CurrentIndex, &ByteCount))
        {
            return CreateInvalidEncodingError(operationName);
        }

        CurrentIndex += ByteCount;
    }

    return Error_CreateSuccess();
}

static Error ValidateBuilder(StringBuilder* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if ((self->_activeStringBuffer == NULL) || (self->_activeTempBuffer == NULL))
    {
        return CreateInvalidStateError();
    }

    return Error_CreateSuccess();
}

static Error ValidateByteBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    if (buffer == NULL)
    {
        return CreateNullArgumentError(argumentName);
    }
    if (buffer->_elementSize != STRING_BUILDER_BYTE_ELEMENT_SIZE)
    {
        return CreateByteBufferTypeError(argumentName, buffer->_elementSize);
    }

    return Error_CreateSuccess();
}

static Error ValidateWritableByteBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    Error Result = ValidateByteBuffer(buffer, argumentName);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (GenericBuffer_IsReadOnly(buffer))
    {
        return CreateReadOnlyBufferError(argumentName);
    }

    return Error_CreateSuccess();
}

static Error EnsureContentCapacity(GenericBuffer* buffer, size_t requiredContentCapacity, const unsigned char* operationName)
{
    size_t RequiredBufferCapacity = 0;

    if (requiredContentCapacity == SIZE_MAX)
    {
        return CreateOverflowError(operationName);
    }

    RequiredBufferCapacity = requiredContentCapacity + 1U;
    if (!GenericBuffer_EnsureTotalCapacity(buffer, RequiredBufferCapacity))
    {
        return CreateBufferTooSmallError(operationName, RequiredBufferCapacity);
    }

    return Error_CreateSuccess();
}

static Error SyncNullTerminator(GenericBuffer* buffer, const unsigned char* operationName)
{
    Error Result = EnsureContentCapacity(buffer, buffer->_count, operationName);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    buffer->_data[buffer->_count] = u8'\0';
    return Error_CreateSuccess();
}

static Error PrepareExternalStringBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    size_t ContentLength = 0;
    Error Result = ValidateWritableByteBuffer(buffer, argumentName);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ContentLength = buffer->_count;
    if ((ContentLength > 0U) && (buffer->_data[ContentLength - 1U] == u8'\0'))
    {
        ContentLength--;
    }

    Result = ValidateUTF8Region(buffer->_data, ContentLength, u8"use the wrapped StringBuilder buffer");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    buffer->_count = ContentLength;
    return SyncNullTerminator(buffer, u8"use the wrapped StringBuilder buffer");
}

static Error PrepareExternalTempBuffer(GenericBuffer* buffer, const unsigned char* argumentName)
{
    Error Result = ValidateWritableByteBuffer(buffer, argumentName);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (buffer->_count > 0U)
    {
        if (buffer->_data[buffer->_count - 1U] == u8'\0')
        {
            buffer->_count--;
        }
        else
        {
            buffer->_count = 0;
        }
    }
    else
    {
        buffer->_count = 0;
    }

    return SyncNullTerminator(buffer, u8"use the wrapped StringBuilder temp buffer");
}

static Error ValidateInsertIndex(StringBuilder* self, size_t index, const unsigned char* operationName)
{
    size_t ByteLength = 0;
    const unsigned char* Text = NULL;
    Error Result = ValidateBuilder(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringBuilder_GetLength(self);
    Text = StringBuilder_GetStringBacked(self);
    if (index > ByteLength)
    {
        return CreateIndexOutOfBoundsError(operationName);
    }
    if (!IsCharacterBoundary(Text, ByteLength, index))
    {
        return CreateInvalidEncodingError(operationName);
    }

    return Error_CreateSuccess();
}

static Error ValidateRange(StringBuilder* self, size_t startIndex, size_t length, const unsigned char* operationName)
{
    size_t ByteLength = 0;
    size_t EndIndex = 0;
    const unsigned char* Text = NULL;
    Error Result = ValidateBuilder(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    ByteLength = StringBuilder_GetLength(self);
    Text = StringBuilder_GetStringBacked(self);
    if (startIndex > ByteLength)
    {
        return CreateIndexOutOfBoundsError(operationName);
    }
    if (length > (ByteLength - startIndex))
    {
        return CreateIndexOutOfBoundsError(operationName);
    }

    EndIndex = startIndex + length;
    if (!IsCharacterBoundary(Text, ByteLength, startIndex)
        || !IsCharacterBoundary(Text, ByteLength, EndIndex))
    {
        return CreateInvalidEncodingError(operationName);
    }

    return Error_CreateSuccess();
}

static Error ClearTempBuffer(StringBuilder* self)
{
    Error Result = ValidateBuilder(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_Clear(self->_activeTempBuffer))
    {
        return CreateReadOnlyBufferError(u8"tempBuffer");
    }

    return SyncNullTerminator(self->_activeTempBuffer, u8"clear the temporary string buffer");
}

static Error StageBytesInTempBuffer(StringBuilder* self,
    const unsigned char* bytes,
    size_t byteCount,
    const unsigned char* operationName)
{
    Error Result = ClearTempBuffer(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((bytes == NULL) && (byteCount > 0U))
    {
        return CreateNullArgumentError(u8"bytes");
    }

    Result = EnsureContentCapacity(self->_activeTempBuffer, byteCount, operationName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((byteCount > 0U) && !GenericBuffer_AddLastRange(self->_activeTempBuffer, (void*)bytes, byteCount))
    {
        return CreateBufferTooSmallError(operationName, byteCount + 1U);
    }

    return SyncNullTerminator(self->_activeTempBuffer, operationName);
}

static Error InsertBytesFromTempBuffer(StringBuilder* self, size_t index, const unsigned char* operationName)
{
    size_t InsertLength = 0;
    Error Result = ValidateInsertIndex(self, index, operationName);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    InsertLength = self->_activeTempBuffer->_count;
    Result = EnsureContentCapacity(self->_activeStringBuffer,
        StringBuilder_GetLength(self) + InsertLength,
        operationName);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((InsertLength > 0U)
        && !GenericBuffer_InsertRange(self->_activeStringBuffer, self->_activeTempBuffer->_data, InsertLength, index))
    {
        return CreateBufferTooSmallError(operationName, StringBuilder_GetLength(self) + InsertLength + 1U);
    }

    return SyncNullTerminator(self->_activeStringBuffer, operationName);
}

static Error InsertByteRegion(StringBuilder* self,
    size_t index,
    const unsigned char* bytes,
    size_t byteCount,
    const unsigned char* operationName)
{
    Error Result = StageBytesInTempBuffer(self, bytes, byteCount, operationName);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return InsertBytesFromTempBuffer(self, index, operationName);
}

static Error InsertFormattedNumber(StringBuilder* self,
    size_t index,
    Error (*writeFunction)(GenericBuffer* tempBuffer, void* userData),
    void* userData,
    const unsigned char* operationName)
{
    Error Result = ClearTempBuffer(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = writeFunction(self->_activeTempBuffer, userData);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((self->_activeTempBuffer->_count == 0U)
        || (self->_activeTempBuffer->_data[self->_activeTempBuffer->_count - 1U] != u8'\0'))
    {
        return CreateInvalidStateError();
    }

    self->_activeTempBuffer->_count--;
    Result = InsertBytesFromTempBuffer(self, index, operationName);
    self->_activeTempBuffer->_count++;
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return SyncNullTerminator(self->_activeTempBuffer, operationName);
}
static Error WriteInt8ToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_Int8ToString(Context->Value.Int8, Context->Base, Context->IncludePrefix, tempBuffer);
}

static Error WriteUInt8ToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_UInt8ToString(Context->Value.UInt8, Context->Base, Context->IncludePrefix, tempBuffer);
}

static Error WriteInt16ToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_Int16ToString(Context->Value.Int16, Context->Base, Context->IncludePrefix, tempBuffer);
}

static Error WriteUInt16ToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_UInt16ToString(Context->Value.UInt16, Context->Base, Context->IncludePrefix, tempBuffer);
}

static Error WriteInt32ToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_Int32ToString(Context->Value.Int32, Context->Base, Context->IncludePrefix, tempBuffer);
}

static Error WriteUInt32ToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_UInt32ToString(Context->Value.UInt32, Context->Base, Context->IncludePrefix, tempBuffer);
}

static Error WriteInt64ToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_Int64ToString(Context->Value.Int64, Context->Base, Context->IncludePrefix, tempBuffer);
}

static Error WriteUInt64ToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_UInt64ToString(Context->Value.UInt64, Context->Base, Context->IncludePrefix, tempBuffer);
}

static Error WriteFloatToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_FloatToString(Context->Value.Float, tempBuffer, Context->Options);
}

static Error WriteDoubleToTemp(GenericBuffer* tempBuffer, void* userData)
{
    IntegerInsertContext* Context = userData;
    return Number_DoubleToString(Context->Value.Double, tempBuffer, Context->Options);
}


// Public functions.
Error StringBuilder_Construct1(StringBuilder* self, size_t initialCapacity)
{
    size_t BufferCapacity = initialCapacity + 1U;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (BufferCapacity == 0U)
    {
        return CreateOverflowError(u8"construct the StringBuilder");
    }

    self->_selfString._data = Memory_Allocate(BufferCapacity * STRING_BUILDER_BYTE_ELEMENT_SIZE);
    self->_selfTempBuffer._data = Memory_Allocate(BufferCapacity * STRING_BUILDER_BYTE_ELEMENT_SIZE);
    GenericBuffer_CreateVariable(&self->_selfString,
        self->_selfString._data,
        BufferCapacity,
        STRING_BUILDER_BYTE_ELEMENT_SIZE,
        0,
        NULL,
        &StringBuilderBufferAllocate);
    GenericBuffer_CreateVariable(&self->_selfTempBuffer,
        self->_selfTempBuffer._data,
        BufferCapacity,
        STRING_BUILDER_BYTE_ELEMENT_SIZE,
        0,
        NULL,
        &StringBuilderBufferAllocate);
    self->_activeStringBuffer = &self->_selfString;
    self->_activeTempBuffer = &self->_selfTempBuffer;
    self->_areBuffersOwned = true;

    self->_selfString._data[0] = u8'\0';
    self->_selfTempBuffer._data[0] = u8'\0';
    return Error_CreateSuccess();
}

Error StringBuilder_Construct2(StringBuilder* self, GenericBuffer* stringBuffer, GenericBuffer* tempBuffer)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    Result = PrepareExternalStringBuffer(stringBuffer, u8"stringBuffer");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = PrepareExternalTempBuffer(tempBuffer, u8"tempBuffer");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_selfString = (GenericBuffer){ 0 };
    self->_selfTempBuffer = (GenericBuffer){ 0 };
    self->_activeStringBuffer = stringBuffer;
    self->_activeTempBuffer = tempBuffer;
    self->_areBuffersOwned = false;
    return Error_CreateSuccess();
}

Error StringBuilder_Deconstruct(StringBuilder* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    if (self->_areBuffersOwned)
    {
        Memory_Free(self->_selfString._data);
        Memory_Free(self->_selfTempBuffer._data);
    }

    self->_selfString = (GenericBuffer){ 0 };
    self->_selfTempBuffer = (GenericBuffer){ 0 };
    self->_activeStringBuffer = NULL;
    self->_activeTempBuffer = NULL;
    self->_areBuffersOwned = false;
    return Error_CreateSuccess();
}

Error StringBuilder_InsertStringFromBuilder(StringBuilder* self, size_t index, StringBuilder* other)
{
    Error Result = ValidateBuilder(other);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return InsertByteRegion(self,
        index,
        StringBuilder_GetStringBacked(other),
        StringBuilder_GetLength(other),
        u8"insert the StringBuilder string");
}

Error StringBuilder_InsertString(StringBuilder* self, size_t index, const unsigned char* str)
{
    size_t Length = 0;

    if (str == NULL)
    {
        return CreateNullArgumentError(u8"str");
    }

    Length = GetByteLength(str);
    return InsertByteRegion(self, index, str, Length, u8"insert the string");
}

Error StringBuilder_InsertSubstring(StringBuilder* self, size_t index, const unsigned char* str, size_t length)
{
    Error Result = ValidateUTF8Region(str, length, u8"insert the substring");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return InsertByteRegion(self, index, str, length, u8"insert the substring");
}

Error StringBuilder_InsertCodePoint(StringBuilder* self, size_t index, CodePoint codePoint)
{
    unsigned char Character[CODEPOINT_BYTE_COUNT_MAX];
    size_t ByteCount = CharUTF8_WriteCodePoint(Character, codePoint);

    if (ByteCount == 0U)
    {
        return CreateInvalidCodePointError(u8"insert the code point");
    }

    return InsertByteRegion(self, index, Character, ByteCount, u8"insert the code point");
}

Error StringBuilder_InsertInt8(StringBuilder* self, size_t index, int8_t value, int32_t base, bool includePrefix)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.Int8 = value;
    Context.Base = base;
    Context.IncludePrefix = includePrefix;
    return InsertFormattedNumber(self, index, &WriteInt8ToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertUInt8(StringBuilder* self, size_t index, uint8_t value, int32_t base, bool includePrefix)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.UInt8 = value;
    Context.Base = base;
    Context.IncludePrefix = includePrefix;
    return InsertFormattedNumber(self, index, &WriteUInt8ToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertInt16(StringBuilder* self, size_t index, int16_t value, int32_t base, bool includePrefix)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.Int16 = value;
    Context.Base = base;
    Context.IncludePrefix = includePrefix;
    return InsertFormattedNumber(self, index, &WriteInt16ToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertUInt16(StringBuilder* self, size_t index, uint16_t value, int32_t base, bool includePrefix)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.UInt16 = value;
    Context.Base = base;
    Context.IncludePrefix = includePrefix;
    return InsertFormattedNumber(self, index, &WriteUInt16ToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertInt32(StringBuilder* self, size_t index, int32_t value, int32_t base, bool includePrefix)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.Int32 = value;
    Context.Base = base;
    Context.IncludePrefix = includePrefix;
    return InsertFormattedNumber(self, index, &WriteInt32ToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertUInt32(StringBuilder* self, size_t index, uint32_t value, int32_t base, bool includePrefix)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.UInt32 = value;
    Context.Base = base;
    Context.IncludePrefix = includePrefix;
    return InsertFormattedNumber(self, index, &WriteUInt32ToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertInt64(StringBuilder* self, size_t index, int64_t value, int32_t base, bool includePrefix)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.Int64 = value;
    Context.Base = base;
    Context.IncludePrefix = includePrefix;
    return InsertFormattedNumber(self, index, &WriteInt64ToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertUInt64(StringBuilder* self, size_t index, uint64_t value, int32_t base, bool includePrefix)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.UInt64 = value;
    Context.Base = base;
    Context.IncludePrefix = includePrefix;
    return InsertFormattedNumber(self, index, &WriteUInt64ToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertFloat(StringBuilder* self, size_t index, float value, DecimalFormatOptions options)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.Float = value;
    Context.Options = options;
    return InsertFormattedNumber(self, index, &WriteFloatToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertDouble(StringBuilder* self, size_t index, double value, DecimalFormatOptions options)
{
    IntegerInsertContext Context = { 0 };
    Context.Value.Double = value;
    Context.Options = options;
    return InsertFormattedNumber(self, index, &WriteDoubleToTemp, &Context, u8"insert the number");
}

Error StringBuilder_InsertBoolean(StringBuilder* self, size_t index, bool value)
{
    const unsigned char* Text = value ? STRING_BUILDER_TRUE : STRING_BUILDER_FALSE;
    return InsertByteRegion(self, index, Text, GetByteLength(Text), u8"insert the boolean");
}

Error StringBuilder_RemoveRange(StringBuilder* self, size_t startIndex, size_t length)
{
    Error Result = ValidateRange(self, startIndex, length, u8"remove the string range");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_RemoveRange(self->_activeStringBuffer, startIndex, startIndex + length))
    {
        return CreateIndexOutOfBoundsError(u8"remove the string range");
    }

    return SyncNullTerminator(self->_activeStringBuffer, u8"remove the string range");
}

Error StringBuilder_Truncate(StringBuilder* self, size_t newLength)
{
    size_t CurrentLength = 0;
    Error Result = ValidateBuilder(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    CurrentLength = StringBuilder_GetLength(self);
    if (newLength > CurrentLength)
    {
        return CreateIndexOutOfBoundsError(u8"truncate the string");
    }

    return StringBuilder_RemoveRange(self, newLength, CurrentLength - newLength);
}

Error StringBuilder_EnsureTotalCapacity(StringBuilder* self, size_t requiredTotalCapacity)
{
    Error Result = ValidateBuilder(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return EnsureContentCapacity(self->_activeStringBuffer, requiredTotalCapacity, u8"ensure StringBuilder capacity");
}

Error StringBuilder_ReserveMoreCapacity(StringBuilder* self, size_t addedCapacity)
{
    size_t RequiredCapacity = 0;
    Error Result = ValidateBuilder(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (addedCapacity > (SIZE_MAX - StringBuilder_GetLength(self)))
    {
        return CreateOverflowError(u8"reserve more StringBuilder capacity");
    }

    RequiredCapacity = StringBuilder_GetLength(self) + addedCapacity;
    return EnsureContentCapacity(self->_activeStringBuffer, RequiredCapacity, u8"reserve more StringBuilder capacity");
}

Error StringBuilder_CopyTo(StringBuilder* self, GenericBuffer* destination)
{
    Error Result = ValidateBuilder(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return StringUTF8_CopyToBySize(StringBuilder_GetStringBacked(self), StringBuilder_GetLength(self), destination);
}
