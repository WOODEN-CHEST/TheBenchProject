#include "WRBinaryIO.h"
#include <inttypes.h>


// Macros.


// Types.


// Fields.
static const IOStreamVTable BINARY_IO_STREAM_VTABLE =
{
    .Self = NULL,
    ._getPosition = NULL,
    ._setPosition = NULL,
    ._setPositionSpecial = NULL,
    ._setLength = NULL,
    ._flush = NULL,
    ._writeByte = NULL,
    ._write = NULL,
    ._readByte = NULL,
    ._read = NULL,
    ._close = NULL,
    ._isEOF = NULL,
    ._deconstruct = NULL,
};

static const BinaryIOStreamVTable BINARY_STREAM_TYPED_VTABLE =
{
    .Self = NULL,
    ._writeInt8 = NULL,
    ._writeUInt8 = NULL,
    ._writeInt16 = NULL,
    ._writeUInt16 = NULL,
    ._writeInt32 = NULL,
    ._writeUInt32 = NULL,
    ._writeInt64 = NULL,
    ._writeUInt64 = NULL,
    ._writeFloat = NULL,
    ._writeDouble = NULL,
    ._writeBoolean = NULL,
    ._writeEncodedInt32 = NULL,
    ._writeEncodedUInt32 = NULL,
    ._writeEncodedInt64 = NULL,
    ._writeEncodedUInt64 = NULL,
    ._readInt8 = NULL,
    ._readUInt8 = NULL,
    ._readInt16 = NULL,
    ._readUInt16 = NULL,
    ._readInt32 = NULL,
    ._readUInt32 = NULL,
    ._readInt64 = NULL,
    ._readUInt64 = NULL,
    ._readFloat = NULL,
    ._readDouble = NULL,
    ._readBoolean = NULL,
    ._readEncodedInt32 = NULL,
    ._readEncodedUInt32 = NULL,
    ._readEncodedInt64 = NULL,
    ._readEncodedUInt64 = NULL,
};


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Binary I/O argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateByteBufferRequiredError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Binary I/O argument \"%s\" must be a byte buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreateInvalidEndianessError(MachineEndianess endianess)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Unsupported machine endianess value %d.",
        (int)endianess);
}

static Error CreateSourceTooSmallError(size_t requiredSize, size_t maxSourceLength)
{
    return Error_Construct3(ErrorCode_BufferTooSmall,
        u8"Cannot read a binary value that requires %zu bytes from a source buffer with only %zu bytes available.",
        requiredSize,
        maxSourceLength);
}

static Error CreateInvalidBooleanError(unsigned char value)
{
    return Error_Construct3(ErrorCode_Deserialize,
        u8"Cannot deserialize a boolean from byte value %u. Only 0 and 1 are supported.",
        (unsigned int)value);
}

static Error CreateInvalidEncodedIntegerError(size_t byteCount, size_t bitWidth)
{
    return Error_Construct3(ErrorCode_DecodeError,
        u8"Cannot decode an encoded integer that uses %zu bytes for a %zu-bit value.",
        byteCount,
        bitWidth);
}

static Error CreateTruncatedEncodedIntegerError(size_t maxSourceLength)
{
    return Error_Construct3(ErrorCode_DecodeError,
        u8"Cannot decode an encoded integer because the source ended after %zu bytes.",
        maxSourceLength);
}

static Error CreateClosedBinaryStreamError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Cannot %s a closed binary stream.",
        operationName);
}

static Error CreateInsufficientStreamDataError(size_t requiredSize, size_t actualSize)
{
    return Error_Construct3(ErrorCode_IO,
        u8"Cannot read %zu bytes from the wrapped stream because only %zu bytes were available.",
        requiredSize,
        actualSize);
}

static Error ValidateBinaryConverter(BinaryConverter* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    switch (self->_targetEndianness)
    {
        case MachineEndianess_LittleEndian:
        case MachineEndianess_BigEndian:
            return Error_CreateSuccess();

        default:
            return CreateInvalidEndianessError(self->_targetEndianness);
    }
}

static bool BinaryConverter_ShouldReverseBytes(BinaryConverter* self)
{
    return (Environment_GetEndianess() != self->_targetEndianness);
}

static void ReverseByteOrder(unsigned char* bytes, size_t byteCount)
{
    size_t HalfCount = byteCount / 2U;

    for (size_t Index = 0; Index < HalfCount; Index++)
    {
        size_t OppositeIndex = (byteCount - 1U) - Index;
        unsigned char Temp = bytes[Index];
        bytes[Index] = bytes[OppositeIndex];
        bytes[OppositeIndex] = Temp;
    }
}

static Error ValidateEncodedIntegerArguments(BinaryConverter* self, GenericBuffer* destination)
{
    Error Result = ValidateBinaryConverter(self);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (destination == NULL)
    {
        return CreateNullArgumentError(u8"destination");
    }
    if (destination->_elementSize != sizeof(unsigned char))
    {
        return CreateByteBufferRequiredError(u8"destination", destination->_elementSize);
    }

    return Error_CreateSuccess();
}

static Error WriteEncodedUnsignedValue(BinaryConverter* self, GenericBuffer* destination, uint64_t value, size_t bitWidth)
{
    Error Result = ValidateEncodedIntegerArguments(self, destination);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((bitWidth == 0U) || (bitWidth > 64U))
    {
        return Error_Construct3(ErrorCode_IllegalArgument,
            u8"Unsupported encoded integer bit width %zu.",
            bitWidth);
    }
    if ((bitWidth < 64U) && (value >= (1ULL << bitWidth)))
    {
        return Error_Construct3(ErrorCode_EncodeError,
            u8"Cannot encode unsigned value %" PRIu64 " in %zu bits.",
            value,
            bitWidth);
    }

    do
    {
        unsigned char ByteValue = (unsigned char)(value & 0x7FULL);
        value >>= 7U;
        if (value != 0U)
        {
            ByteValue = (unsigned char)(ByteValue | 0x80U);
        }
        if (!GenericBuffer_AppendByte(destination, ByteValue))
        {
            return Error_Construct1(ErrorCode_BufferTooSmall,
                u8"Could not append an encoded integer byte to the destination buffer.");
        }
    } while (value != 0U);

    return Error_CreateSuccess();
}

static Error ReadEncodedUnsignedValue(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    uint64_t* value,
    size_t* outBytesRead,
    size_t maxByteCount,
    size_t bitWidth)
{
    Error Result = ValidateBinaryConverter(self);
    uint64_t DecodedValue = 0U;

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (source == NULL)
    {
        return CreateNullArgumentError(u8"source");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }
    if (outBytesRead == NULL)
    {
        return CreateNullArgumentError(u8"outBytesRead");
    }

    *value = 0U;
    *outBytesRead = 0U;
    for (size_t Index = 0; Index < maxByteCount; Index++)
    {
        size_t Shift = Index * 7U;
        unsigned char ByteValue = 0U;
        uint64_t Payload = 0U;
        uint64_t AllowedMask = 0U;

        if (Index >= maxSourceLength)
        {
            return CreateTruncatedEncodedIntegerError(maxSourceLength);
        }

        ByteValue = source[Index];
        Payload = (uint64_t)(ByteValue & 0x7FU);
        if (Shift >= bitWidth)
        {
            return CreateInvalidEncodedIntegerError(Index + 1U, bitWidth);
        }

        if ((bitWidth - Shift) >= 7U)
        {
            AllowedMask = 0x7FU;
        }
        else
        {
            AllowedMask = (1ULL << (bitWidth - Shift)) - 1ULL;
        }
        if ((Payload & (~AllowedMask)) != 0U)
        {
            return CreateInvalidEncodedIntegerError(Index + 1U, bitWidth);
        }

        DecodedValue |= (Payload << Shift);
        if ((ByteValue & 0x80U) == 0U)
        {
            *value = DecodedValue;
            *outBytesRead = Index + 1U;
            return Error_CreateSuccess();
        }
    }

    return CreateInvalidEncodedIntegerError(maxByteCount, bitWidth);
}

static Error WriteBinaryValue(BinaryConverter* self, GenericBuffer* destination, const void* value, size_t valueSize)
{
    Error Result = ValidateBinaryConverter(self);
    unsigned char TempBuffer[sizeof(double)];

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (destination == NULL)
    {
        return CreateNullArgumentError(u8"destination");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }
    if (destination->_elementSize != sizeof(unsigned char))
    {
        return CreateByteBufferRequiredError(u8"destination", destination->_elementSize);
    }
    if (valueSize > sizeof(TempBuffer))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge,
            u8"Cannot write a binary value of %zu bytes because the converter temporary buffer is too small.",
            valueSize);
    }
    if (!GenericBuffer_TryPrepareForManualMutation(destination, valueSize))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Destination buffer is too small to hold a binary value of %zu bytes.",
            valueSize);
    }

    Memory_Copy(value, TempBuffer, valueSize);
    if (BinaryConverter_ShouldReverseBytes(self))
    {
        ReverseByteOrder(TempBuffer, valueSize);
    }

    Memory_Copy(TempBuffer, destination->_data + destination->_count, valueSize);
    destination->_count += valueSize;
    return Error_CreateSuccess();
}

static Error ReadBinaryValue(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, void* value, size_t valueSize)
{
    Error Result = ValidateBinaryConverter(self);
    unsigned char TempBuffer[sizeof(double)];

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (source == NULL)
    {
        return CreateNullArgumentError(u8"source");
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }
    if (maxSourceLength < valueSize)
    {
        return CreateSourceTooSmallError(valueSize, maxSourceLength);
    }
    if (valueSize > sizeof(TempBuffer))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge,
            u8"Cannot read a binary value of %zu bytes because the converter temporary buffer is too small.",
            valueSize);
    }

    Memory_Copy(source, TempBuffer, valueSize);
    if (BinaryConverter_ShouldReverseBytes(self))
    {
        ReverseByteOrder(TempBuffer, valueSize);
    }

    Memory_Copy(TempBuffer, value, valueSize);
    return Error_CreateSuccess();
}

static Error BinaryIOStream_EnsureOpen(BinaryIOStream* self, const unsigned char* operationName)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (self->_isClosed)
    {
        return CreateClosedBinaryStreamError(operationName);
    }
    if (self->_wrappedStream == NULL)
    {
        return Error_Construct1(ErrorCode_InvalidState, u8"Cannot use a binary stream without a wrapped stream.");
    }

    return Error_CreateSuccess();
}

static void CreateTempByteBuffer(GenericBuffer* buffer, unsigned char* data, size_t capacity)
{
    GenericBuffer_CreateVariable(buffer,
        data,
        capacity,
        sizeof(unsigned char),
        0,
        NULL,
        NULL);
}

static Error BinaryIOStream_ReadExact(BinaryIOStream* self, size_t byteCount)
{
    GenericBuffer Buffer;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"read from");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (byteCount > BINARY_STREAM_TEMP_BUFFER_SIZE)
    {
        return Error_Construct3(ErrorCode_BufferTooLarge,
            u8"Cannot read %zu bytes through the binary stream because its temporary buffer is too small.",
            byteCount);
    }

    CreateTempByteBuffer(&Buffer, self->_tempBuffer, BINARY_STREAM_TEMP_BUFFER_SIZE);
    Result = IOStream_Read(self->_wrappedStream, byteCount, &Buffer);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Buffer._count != byteCount)
    {
        return CreateInsufficientStreamDataError(byteCount, Buffer._count);
    }

    return Error_CreateSuccess();
}

static Error BinaryIOStream_WriteConverted(BinaryIOStream* self, const void* value, size_t valueSize)
{
    GenericBuffer Buffer;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"write to");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (valueSize > BINARY_STREAM_TEMP_BUFFER_SIZE)
    {
        return Error_Construct3(ErrorCode_BufferTooLarge,
            u8"Cannot write %zu bytes through the binary stream because its temporary buffer is too small.",
            valueSize);
    }

    CreateTempByteBuffer(&Buffer, self->_tempBuffer, BINARY_STREAM_TEMP_BUFFER_SIZE);
    Result = WriteBinaryValue(&self->_converter, &Buffer, value, valueSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_Write(self->_wrappedStream, Buffer._data, Buffer._count);
}

static Error BinaryIOStream_ReadConverted(BinaryIOStream* self, void* value, size_t valueSize)
{
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Error Result = BinaryIOStream_ReadExact(self, valueSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return ReadBinaryValue(&self->_converter, self->_tempBuffer, valueSize, value, valueSize);
}

static Error BinaryIOStream_WriteEncodedFromBuffer(BinaryIOStream* self, GenericBuffer* buffer)
{
    Error Result = BinaryIOStream_EnsureOpen(self, u8"write to");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }

    return IOStream_Write(self->_wrappedStream, buffer->_data, buffer->_count);
}

static Error BinaryIOStream_ReadEncodedUnsigned(BinaryIOStream* self,
    uint64_t* value,
    size_t maxByteCount,
    size_t bitWidth,
    size_t* outBytesRead)
{
    Error Result = BinaryIOStream_EnsureOpen(self, u8"read from");
    uint64_t DecodedValue = 0U;

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }
    if (outBytesRead == NULL)
    {
        return CreateNullArgumentError(u8"outBytesRead");
    }

    *value = 0U;
    *outBytesRead = 0U;
    for (size_t Index = 0; Index < maxByteCount; Index++)
    {
        size_t Shift = Index * 7U;
        unsigned char ByteValue = 0U;
        uint64_t Payload = 0U;
        uint64_t AllowedMask = 0U;

        Result = IOStream_ReadByte(self->_wrappedStream, &ByteValue);
        if (Result.Code != ErrorCode_Success)
        {
            return CreateTruncatedEncodedIntegerError(Index);
        }

        Payload = (uint64_t)(ByteValue & 0x7FU);
        if (Shift >= bitWidth)
        {
            return CreateInvalidEncodedIntegerError(Index + 1U, bitWidth);
        }

        if ((bitWidth - Shift) >= 7U)
        {
            AllowedMask = 0x7FU;
        }
        else
        {
            AllowedMask = (1ULL << (bitWidth - Shift)) - 1ULL;
        }
        if ((Payload & (~AllowedMask)) != 0U)
        {
            return CreateInvalidEncodedIntegerError(Index + 1U, bitWidth);
        }

        DecodedValue |= (Payload << Shift);
        if ((ByteValue & 0x80U) == 0U)
        {
            *value = DecodedValue;
            *outBytesRead = Index + 1U;
            return Error_CreateSuccess();
        }
    }

    return CreateInvalidEncodedIntegerError(maxByteCount, bitWidth);
}

static Error BinaryIOStream_GetPosition(void* selfVoid, size_t* position)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"get the position of");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_GetPosition(self->_wrappedStream, position);
}

static Error BinaryIOStream_SetPosition(void* selfVoid, size_t position)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"set the position of");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_SetPosition(self->_wrappedStream, position);
}

static Error BinaryIOStream_SetPositionSpecial(void* selfVoid, IOStreamSeekOrigin origin)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"set the special position of");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_SetPositionSpecial(self->_wrappedStream, origin);
}

static Error BinaryIOStream_SetLengthRaw(void* selfVoid, size_t length)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"set the length of");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_SetLength(self->_wrappedStream, length);
}

static Error BinaryIOStream_Flush(void* selfVoid)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"flush");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_Flush(self->_wrappedStream);
}

static Error BinaryIOStream_WriteByteRaw(void* selfVoid, unsigned char byte)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"write to");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_WriteByte(self->_wrappedStream, byte);
}

static Error BinaryIOStream_WriteRaw(void* selfVoid, const unsigned char* buffer, size_t bufferSize)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"write to");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_Write(self->_wrappedStream, buffer, bufferSize);
}

static Error BinaryIOStream_ReadByteRaw(void* selfVoid, unsigned char* byte)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"read from");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_ReadByte(self->_wrappedStream, byte);
}

static Error BinaryIOStream_ReadRaw(void* selfVoid, GenericBuffer* dest, size_t readSize)
{
    BinaryIOStream* self = selfVoid;
    Error Result = BinaryIOStream_EnsureOpen(self, u8"read from");
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return IOStream_Read(self->_wrappedStream, readSize, dest);
}

static Error BinaryIOStream_CloseRaw(void* selfVoid)
{
    BinaryIOStream* self = selfVoid;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (self->_isClosed)
    {
        return Error_CreateSuccess();
    }

    if (self->_ownsWrappedStream)
    {
        Result = IOStream_Close(self->_wrappedStream);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    self->_isClosed = true;
    return Error_CreateSuccess();
}

static bool BinaryIOStream_IsEOFRaw(void* selfVoid)
{
    BinaryIOStream* self = selfVoid;

    if ((self == NULL) || self->_isClosed || (self->_wrappedStream == NULL))
    {
        return true;
    }

    return IOStream_IsEndOfStream(self->_wrappedStream);
}

static Error BinaryIOStream_VTableDeconstruct(void* selfVoid)
{
    BinaryIOStream* self = selfVoid;
    return BinaryIOStream_Deconstruct(self);
}

static IOStreamVTable CreateBinaryIOStreamVTable(BinaryIOStream* self)
{
    IOStreamVTable Result = BINARY_IO_STREAM_VTABLE;
    Result.Self = self;
    Result._getPosition = &BinaryIOStream_GetPosition;
    Result._setPosition = &BinaryIOStream_SetPosition;
    Result._setPositionSpecial = &BinaryIOStream_SetPositionSpecial;
    Result._setLength = &BinaryIOStream_SetLengthRaw;
    Result._flush = &BinaryIOStream_Flush;
    Result._writeByte = &BinaryIOStream_WriteByteRaw;
    Result._write = &BinaryIOStream_WriteRaw;
    Result._readByte = &BinaryIOStream_ReadByteRaw;
    Result._read = &BinaryIOStream_ReadRaw;
    Result._close = &BinaryIOStream_CloseRaw;
    Result._isEOF = &BinaryIOStream_IsEOFRaw;
    Result._deconstruct = &BinaryIOStream_VTableDeconstruct;
    return Result;
}

static Error BinaryIOStream_WriteInt8VTable(void* selfVoid, int8_t value)
{
    return BinaryIOStream_WriteInt8(selfVoid, value);
}

static Error BinaryIOStream_WriteUInt8VTable(void* selfVoid, uint8_t value)
{
    return BinaryIOStream_WriteUInt8(selfVoid, value);
}

static Error BinaryIOStream_WriteInt16VTable(void* selfVoid, int16_t value)
{
    return BinaryIOStream_WriteInt16(selfVoid, value);
}

static Error BinaryIOStream_WriteUInt16VTable(void* selfVoid, uint16_t value)
{
    return BinaryIOStream_WriteUInt16(selfVoid, value);
}

static Error BinaryIOStream_WriteInt32VTable(void* selfVoid, int32_t value)
{
    return BinaryIOStream_WriteInt32(selfVoid, value);
}

static Error BinaryIOStream_WriteUInt32VTable(void* selfVoid, uint32_t value)
{
    return BinaryIOStream_WriteUInt32(selfVoid, value);
}

static Error BinaryIOStream_WriteInt64VTable(void* selfVoid, int64_t value)
{
    return BinaryIOStream_WriteInt64(selfVoid, value);
}

static Error BinaryIOStream_WriteUInt64VTable(void* selfVoid, uint64_t value)
{
    return BinaryIOStream_WriteUInt64(selfVoid, value);
}

static Error BinaryIOStream_WriteFloatVTable(void* selfVoid, float value)
{
    return BinaryIOStream_WriteFloat(selfVoid, value);
}

static Error BinaryIOStream_WriteDoubleVTable(void* selfVoid, double value)
{
    return BinaryIOStream_WriteDouble(selfVoid, value);
}

static Error BinaryIOStream_WriteBooleanVTable(void* selfVoid, bool value)
{
    return BinaryIOStream_WriteBoolean(selfVoid, value);
}

static Error BinaryIOStream_WriteEncodedInt32VTable(void* selfVoid, int32_t value)
{
    return BinaryIOStream_WriteEncodedInt32(selfVoid, value);
}

static Error BinaryIOStream_WriteEncodedUInt32VTable(void* selfVoid, uint32_t value)
{
    return BinaryIOStream_WriteEncodedUInt32(selfVoid, value);
}

static Error BinaryIOStream_WriteEncodedInt64VTable(void* selfVoid, int64_t value)
{
    return BinaryIOStream_WriteEncodedInt64(selfVoid, value);
}

static Error BinaryIOStream_WriteEncodedUInt64VTable(void* selfVoid, uint64_t value)
{
    return BinaryIOStream_WriteEncodedUInt64(selfVoid, value);
}

static Error BinaryIOStream_ReadInt8VTable(void* selfVoid, int8_t* value)
{
    return BinaryIOStream_ReadInt8(selfVoid, value);
}

static Error BinaryIOStream_ReadUInt8VTable(void* selfVoid, uint8_t* value)
{
    return BinaryIOStream_ReadUInt8(selfVoid, value);
}

static Error BinaryIOStream_ReadInt16VTable(void* selfVoid, int16_t* value)
{
    return BinaryIOStream_ReadInt16(selfVoid, value);
}

static Error BinaryIOStream_ReadUInt16VTable(void* selfVoid, uint16_t* value)
{
    return BinaryIOStream_ReadUInt16(selfVoid, value);
}

static Error BinaryIOStream_ReadInt32VTable(void* selfVoid, int32_t* value)
{
    return BinaryIOStream_ReadInt32(selfVoid, value);
}

static Error BinaryIOStream_ReadUInt32VTable(void* selfVoid, uint32_t* value)
{
    return BinaryIOStream_ReadUInt32(selfVoid, value);
}

static Error BinaryIOStream_ReadInt64VTable(void* selfVoid, int64_t* value)
{
    return BinaryIOStream_ReadInt64(selfVoid, value);
}

static Error BinaryIOStream_ReadUInt64VTable(void* selfVoid, uint64_t* value)
{
    return BinaryIOStream_ReadUInt64(selfVoid, value);
}

static Error BinaryIOStream_ReadFloatVTable(void* selfVoid, float* value)
{
    return BinaryIOStream_ReadFloat(selfVoid, value);
}

static Error BinaryIOStream_ReadDoubleVTable(void* selfVoid, double* value)
{
    return BinaryIOStream_ReadDouble(selfVoid, value);
}

static Error BinaryIOStream_ReadBooleanVTable(void* selfVoid, bool* value)
{
    return BinaryIOStream_ReadBoolean(selfVoid, value);
}

static Error BinaryIOStream_ReadEncodedInt32VTable(void* selfVoid, int32_t* value)
{
    return BinaryIOStream_ReadEncodedInt32(selfVoid, value);
}

static Error BinaryIOStream_ReadEncodedUInt32VTable(void* selfVoid, uint32_t* value)
{
    return BinaryIOStream_ReadEncodedUInt32(selfVoid, value);
}

static Error BinaryIOStream_ReadEncodedInt64VTable(void* selfVoid, int64_t* value)
{
    return BinaryIOStream_ReadEncodedInt64(selfVoid, value);
}

static Error BinaryIOStream_ReadEncodedUInt64VTable(void* selfVoid, uint64_t* value)
{
    return BinaryIOStream_ReadEncodedUInt64(selfVoid, value);
}

static BinaryIOStreamVTable CreateBinaryStreamTypedVTable(BinaryIOStream* self)
{
    BinaryIOStreamVTable Result = BINARY_STREAM_TYPED_VTABLE;
    Result.Self = self;
    Result._writeInt8 = &BinaryIOStream_WriteInt8VTable;
    Result._writeUInt8 = &BinaryIOStream_WriteUInt8VTable;
    Result._writeInt16 = &BinaryIOStream_WriteInt16VTable;
    Result._writeUInt16 = &BinaryIOStream_WriteUInt16VTable;
    Result._writeInt32 = &BinaryIOStream_WriteInt32VTable;
    Result._writeUInt32 = &BinaryIOStream_WriteUInt32VTable;
    Result._writeInt64 = &BinaryIOStream_WriteInt64VTable;
    Result._writeUInt64 = &BinaryIOStream_WriteUInt64VTable;
    Result._writeFloat = &BinaryIOStream_WriteFloatVTable;
    Result._writeDouble = &BinaryIOStream_WriteDoubleVTable;
    Result._writeBoolean = &BinaryIOStream_WriteBooleanVTable;
    Result._writeEncodedInt32 = &BinaryIOStream_WriteEncodedInt32VTable;
    Result._writeEncodedUInt32 = &BinaryIOStream_WriteEncodedUInt32VTable;
    Result._writeEncodedInt64 = &BinaryIOStream_WriteEncodedInt64VTable;
    Result._writeEncodedUInt64 = &BinaryIOStream_WriteEncodedUInt64VTable;
    Result._readInt8 = &BinaryIOStream_ReadInt8VTable;
    Result._readUInt8 = &BinaryIOStream_ReadUInt8VTable;
    Result._readInt16 = &BinaryIOStream_ReadInt16VTable;
    Result._readUInt16 = &BinaryIOStream_ReadUInt16VTable;
    Result._readInt32 = &BinaryIOStream_ReadInt32VTable;
    Result._readUInt32 = &BinaryIOStream_ReadUInt32VTable;
    Result._readInt64 = &BinaryIOStream_ReadInt64VTable;
    Result._readUInt64 = &BinaryIOStream_ReadUInt64VTable;
    Result._readFloat = &BinaryIOStream_ReadFloatVTable;
    Result._readDouble = &BinaryIOStream_ReadDoubleVTable;
    Result._readBoolean = &BinaryIOStream_ReadBooleanVTable;
    Result._readEncodedInt32 = &BinaryIOStream_ReadEncodedInt32VTable;
    Result._readEncodedUInt32 = &BinaryIOStream_ReadEncodedUInt32VTable;
    Result._readEncodedInt64 = &BinaryIOStream_ReadEncodedInt64VTable;
    Result._readEncodedUInt64 = &BinaryIOStream_ReadEncodedUInt64VTable;
    return Result;
}


// Public functions.
Error BinaryConverter_Construct1(BinaryConverter* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    self->_targetEndianness = Environment_GetEndianess();
    return Error_CreateSuccess();
}

Error BinaryConverter_Construct2(BinaryConverter* self, MachineEndianess targetEndianness)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    self->_targetEndianness = targetEndianness;
    return ValidateBinaryConverter(self);
}

Error BinaryConverter_WriteInt8(BinaryConverter* self, GenericBuffer* destination, int8_t value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteUInt8(BinaryConverter* self, GenericBuffer* destination, uint8_t value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteInt16(BinaryConverter* self, GenericBuffer* destination, int16_t value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteUInt16(BinaryConverter* self, GenericBuffer* destination, uint16_t value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteInt32(BinaryConverter* self, GenericBuffer* destination, int32_t value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteUInt32(BinaryConverter* self, GenericBuffer* destination, uint32_t value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteInt64(BinaryConverter* self, GenericBuffer* destination, int64_t value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteUInt64(BinaryConverter* self, GenericBuffer* destination, uint64_t value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteFloat(BinaryConverter* self, GenericBuffer* destination, float value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteDouble(BinaryConverter* self, GenericBuffer* destination, double value)
{
    return WriteBinaryValue(self, destination, &value, sizeof(value));
}

Error BinaryConverter_WriteBoolean(BinaryConverter* self, GenericBuffer* destination, bool value)
{
    unsigned char ByteValue = value ? 1U : 0U;
    return WriteBinaryValue(self, destination, &ByteValue, sizeof(ByteValue));
}

Error BinaryConverter_ReadInt8(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int8_t* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadUInt8(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint8_t* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadInt16(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int16_t* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadUInt16(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint16_t* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadInt32(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int32_t* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadUInt32(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint32_t* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadInt64(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int64_t* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadUInt64(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint64_t* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadFloat(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, float* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadDouble(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, double* value)
{
    return ReadBinaryValue(self, source, maxSourceLength, value, sizeof(*value));
}

Error BinaryConverter_ReadBoolean(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, bool* value)
{
    unsigned char ByteValue = 0;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = ReadBinaryValue(self, source, maxSourceLength, &ByteValue, sizeof(ByteValue));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((ByteValue != 0U) && (ByteValue != 1U))
    {
        return CreateInvalidBooleanError(ByteValue);
    }

    *value = (ByteValue == 1U);
    return Error_CreateSuccess();
}

Error BinaryConverter_EncodeInt32(BinaryConverter* self, GenericBuffer* destination, int32_t value)
{
    uint32_t UnsignedValue = 0U;

    Memory_Copy(&value, &UnsignedValue, sizeof(UnsignedValue));
    return WriteEncodedUnsignedValue(self, destination, (uint64_t)UnsignedValue, 32U);
}

Error BinaryConverter_EncodeUInt32(BinaryConverter* self, GenericBuffer* destination, uint32_t value)
{
    return WriteEncodedUnsignedValue(self, destination, (uint64_t)value, 32U);
}

Error BinaryConverter_EncodeInt64(BinaryConverter* self, GenericBuffer* destination, int64_t value)
{
    uint64_t UnsignedValue = 0U;

    Memory_Copy(&value, &UnsignedValue, sizeof(UnsignedValue));
    return WriteEncodedUnsignedValue(self, destination, UnsignedValue, 64U);
}

Error BinaryConverter_EncodeUInt64(BinaryConverter* self, GenericBuffer* destination, uint64_t value)
{
    return WriteEncodedUnsignedValue(self, destination, value, 64U);
}

Error BinaryConverter_DecodeInt32(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    int32_t* value,
    size_t* outBytesRead)
{
    uint64_t UnsignedValue64 = 0U;
    uint32_t UnsignedValue = 0U;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = ReadEncodedUnsignedValue(self, source, maxSourceLength, &UnsignedValue64, outBytesRead, 5U, 32U);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    UnsignedValue = (uint32_t)UnsignedValue64;
    Memory_Copy(&UnsignedValue, value, sizeof(*value));
    return Error_CreateSuccess();
}

Error BinaryConverter_DecodeUInt32(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    uint32_t* value,
    size_t* outBytesRead)
{
    uint64_t UnsignedValue = 0U;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = ReadEncodedUnsignedValue(self, source, maxSourceLength, &UnsignedValue, outBytesRead, 5U, 32U);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *value = (uint32_t)UnsignedValue;
    return Error_CreateSuccess();
}

Error BinaryConverter_DecodeInt64(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    int64_t* value,
    size_t* outBytesRead)
{
    uint64_t UnsignedValue = 0U;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = ReadEncodedUnsignedValue(self, source, maxSourceLength, &UnsignedValue, outBytesRead, 10U, 64U);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Copy(&UnsignedValue, value, sizeof(*value));
    return Error_CreateSuccess();
}

Error BinaryConverter_DecodeUInt64(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    uint64_t* value,
    size_t* outBytesRead)
{
    return ReadEncodedUnsignedValue(self, source, maxSourceLength, value, outBytesRead, 10U, 64U);
}

void BinaryConverter_Deconstruct(BinaryConverter* self)
{
    if (self == NULL)
    {
        return;
    }

    Memory_Zero(self, sizeof(*self));
}

Error BinaryIOStream_Construct1(BinaryIOStream* self, IOStream* wrappedStream, bool ownsWrappedStream)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (wrappedStream == NULL)
    {
        return CreateNullArgumentError(u8"wrappedStream");
    }

    Memory_Zero(self, sizeof(*self));
    Result = BinaryConverter_Construct1(&self->_converter);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->Base._type = wrappedStream->_type;
    self->Base._flags = wrappedStream->_flags;
    self->Base._vtable = CreateBinaryIOStreamVTable(self);
    self->_wrappedStream = wrappedStream;
    self->_vtable = CreateBinaryStreamTypedVTable(self);
    self->_isClosed = false;
    self->_ownsWrappedStream = ownsWrappedStream;
    return Error_CreateSuccess();
}

Error BinaryIOStream_Construct2(BinaryIOStream* self, IOStream* wrappedStream, MachineEndianess targetEndianness, bool ownsWrappedStream)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (wrappedStream == NULL)
    {
        return CreateNullArgumentError(u8"wrappedStream");
    }

    Memory_Zero(self, sizeof(*self));
    Result = BinaryConverter_Construct2(&self->_converter, targetEndianness);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->Base._type = wrappedStream->_type;
    self->Base._flags = wrappedStream->_flags;
    self->Base._vtable = CreateBinaryIOStreamVTable(self);
    self->_wrappedStream = wrappedStream;
    self->_vtable = CreateBinaryStreamTypedVTable(self);
    self->_isClosed = false;
    self->_ownsWrappedStream = ownsWrappedStream;
    return Error_CreateSuccess();
}

Error BinaryIOStream_WriteInt8(BinaryIOStream* self, int8_t value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteUInt8(BinaryIOStream* self, uint8_t value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteInt16(BinaryIOStream* self, int16_t value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteUInt16(BinaryIOStream* self, uint16_t value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteInt32(BinaryIOStream* self, int32_t value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteUInt32(BinaryIOStream* self, uint32_t value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteInt64(BinaryIOStream* self, int64_t value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteUInt64(BinaryIOStream* self, uint64_t value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteFloat(BinaryIOStream* self, float value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteDouble(BinaryIOStream* self, double value)
{
    return BinaryIOStream_WriteConverted(self, &value, sizeof(value));
}

Error BinaryIOStream_WriteBoolean(BinaryIOStream* self, bool value)
{
    unsigned char ByteValue = value ? 1U : 0U;
    return BinaryIOStream_WriteConverted(self, &ByteValue, sizeof(ByteValue));
}

Error BinaryIOStream_WriteEncodedInt32(BinaryIOStream* self, int32_t value)
{
    GenericBuffer Buffer;
    unsigned char Bytes[5];
    Error Result = Error_CreateSuccess();

    CreateTempByteBuffer(&Buffer, Bytes, 5U);
    Result = BinaryConverter_EncodeInt32(&self->_converter, &Buffer, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return BinaryIOStream_WriteEncodedFromBuffer(self, &Buffer);
}

Error BinaryIOStream_WriteEncodedUInt32(BinaryIOStream* self, uint32_t value)
{
    GenericBuffer Buffer;
    unsigned char Bytes[5];
    Error Result = Error_CreateSuccess();

    CreateTempByteBuffer(&Buffer, Bytes, 5U);
    Result = BinaryConverter_EncodeUInt32(&self->_converter, &Buffer, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return BinaryIOStream_WriteEncodedFromBuffer(self, &Buffer);
}

Error BinaryIOStream_WriteEncodedInt64(BinaryIOStream* self, int64_t value)
{
    GenericBuffer Buffer;
    unsigned char Bytes[10];
    Error Result = Error_CreateSuccess();

    CreateTempByteBuffer(&Buffer, Bytes, 10U);
    Result = BinaryConverter_EncodeInt64(&self->_converter, &Buffer, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return BinaryIOStream_WriteEncodedFromBuffer(self, &Buffer);
}

Error BinaryIOStream_WriteEncodedUInt64(BinaryIOStream* self, uint64_t value)
{
    GenericBuffer Buffer;
    unsigned char Bytes[10];
    Error Result = Error_CreateSuccess();

    CreateTempByteBuffer(&Buffer, Bytes, 10U);
    Result = BinaryConverter_EncodeUInt64(&self->_converter, &Buffer, value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return BinaryIOStream_WriteEncodedFromBuffer(self, &Buffer);
}

Error BinaryIOStream_ReadInt8(BinaryIOStream* self, int8_t* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadUInt8(BinaryIOStream* self, uint8_t* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadInt16(BinaryIOStream* self, int16_t* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadUInt16(BinaryIOStream* self, uint16_t* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadInt32(BinaryIOStream* self, int32_t* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadUInt32(BinaryIOStream* self, uint32_t* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadInt64(BinaryIOStream* self, int64_t* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadUInt64(BinaryIOStream* self, uint64_t* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadFloat(BinaryIOStream* self, float* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadDouble(BinaryIOStream* self, double* value)
{
    return BinaryIOStream_ReadConverted(self, value, sizeof(*value));
}

Error BinaryIOStream_ReadBoolean(BinaryIOStream* self, bool* value)
{
    unsigned char ByteValue = 0;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = BinaryIOStream_ReadConverted(self, &ByteValue, sizeof(ByteValue));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((ByteValue != 0U) && (ByteValue != 1U))
    {
        return CreateInvalidBooleanError(ByteValue);
    }

    *value = (ByteValue == 1U);
    return Error_CreateSuccess();
}

Error BinaryIOStream_ReadEncodedInt32(BinaryIOStream* self, int32_t* value)
{
    uint64_t UnsignedValue = 0U;
    size_t BytesRead = 0U;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = BinaryIOStream_ReadEncodedUnsigned(self, &UnsignedValue, 5U, 32U, &BytesRead);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    uint32_t UnsignedValue32 = (uint32_t)UnsignedValue;
    Memory_Copy(&UnsignedValue32, value, sizeof(*value));
    return Error_CreateSuccess();
}

Error BinaryIOStream_ReadEncodedUInt32(BinaryIOStream* self, uint32_t* value)
{
    uint64_t UnsignedValue = 0U;
    size_t BytesRead = 0U;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = BinaryIOStream_ReadEncodedUnsigned(self, &UnsignedValue, 5U, 32U, &BytesRead);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    *value = (uint32_t)UnsignedValue;
    return Error_CreateSuccess();
}

Error BinaryIOStream_ReadEncodedInt64(BinaryIOStream* self, int64_t* value)
{
    uint64_t UnsignedValue = 0U;
    size_t BytesRead = 0U;
    Error Result = Error_CreateSuccess();

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    Result = BinaryIOStream_ReadEncodedUnsigned(self, &UnsignedValue, 10U, 64U, &BytesRead);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Copy(&UnsignedValue, value, sizeof(*value));
    return Error_CreateSuccess();
}

Error BinaryIOStream_ReadEncodedUInt64(BinaryIOStream* self, uint64_t* value)
{
    size_t BytesRead = 0U;

    if (value == NULL)
    {
        return CreateNullArgumentError(u8"value");
    }

    return BinaryIOStream_ReadEncodedUnsigned(self, value, 10U, 64U, &BytesRead);
}

Error BinaryIOStream_Deconstruct(BinaryIOStream* self)
{
    IOStream* WrappedStream = NULL;
    bool OwnsWrappedStream = false;
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    WrappedStream = self->_wrappedStream;
    OwnsWrappedStream = self->_ownsWrappedStream;
    Result = BinaryIOStream_CloseRaw(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    BinaryConverter_Deconstruct(&self->_converter);
    if (OwnsWrappedStream && (WrappedStream != NULL))
    {
        Result = IOStream_Deconstruct(WrappedStream);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    Memory_Zero(self, sizeof(*self));
    return Error_CreateSuccess();
}
