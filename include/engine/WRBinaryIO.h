#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include "WREnvironment.h"
#include <stdint.h>
#include <stddef.h>
#include "WRIO.h"

#define BINARY_STREAM_TEMP_BUFFER_SIZE 16


// Types.
typedef struct BinaryConverterStruct
{
    MachineEndianess _targetEndianness;
} BinaryConverter;

typedef struct BinaryIOStreamVTableStruct
{
    void* Self;
    Error (*_writeInt8)(void* self, int8_t value);
    Error (*_writeUInt8)(void* self, uint8_t value);
    Error (*_writeInt16)(void* self, int16_t value);
    Error (*_writeUInt16)(void* self, uint16_t value);
    Error (*_writeInt32)(void* self, int32_t value);
    Error (*_writeUInt32)(void* self, uint32_t value);
    Error (*_writeInt64)(void* self, int64_t value);
    Error (*_writeUInt64)(void* self, uint64_t value);
    Error (*_writeFloat)(void* self, float value);
    Error (*_writeDouble)(void* self, double value);
    Error (*_writeBoolean)(void* self, bool value);
    Error (*_writeEncodedInt32)(void* self, int32_t value);
    Error (*_writeEncodedUInt32)(void* self, uint32_t value);
    Error (*_writeEncodedInt64)(void* self, int64_t value);
    Error (*_writeEncodedUInt64)(void* self, uint64_t value);

    Error (*_readInt8)(void* self, int8_t* value);
    Error (*_readUInt8)(void* self, uint8_t* value);
    Error (*_readInt16)(void* self, int16_t* value);
    Error (*_readUInt16)(void* self, uint16_t* value);
    Error (*_readInt32)(void* self, int32_t* value);
    Error (*_readUInt32)(void* self, uint32_t* value);
    Error (*_readInt64)(void* self, int64_t* value);
    Error (*_readUInt64)(void* self, uint64_t* value);
    Error (*_readFloat)(void* self, float* value);
    Error (*_readDouble)(void* self, double* value);
    Error (*_readBoolean)(void* self, bool* value);
    Error (*_readEncodedInt32)(void* self, int32_t* value);
    Error (*_readEncodedUInt32)(void* self, uint32_t* value);
    Error (*_readEncodedInt64)(void* self, int64_t* value);
    Error (*_readEncodedUInt64)(void* self, uint64_t* value);
} BinaryIOStreamVTable;

typedef struct BinaryIOStreamStruct
{
    IOStream Base;
    IOStream* _wrappedStream;
    BinaryConverter _converter;
    BinaryIOStreamVTable _vtable;
    unsigned char _tempBuffer[BINARY_STREAM_TEMP_BUFFER_SIZE];
    bool _isClosed;
    bool _ownsWrappedStream;
} BinaryIOStream;



// Functions.

/* Constructs with the machine's endianness as the target endianness. */
Error BinaryConverter_Construct1(BinaryConverter* self);

Error BinaryConverter_Construct2(BinaryConverter* self, MachineEndianess targetEndianness);

static inline Error BinaryConverter_SetTargetEndianness(BinaryConverter* self, MachineEndianess targetEndianness)
{
    self->_targetEndianness = targetEndianness;
    return Error_CreateSuccess();
}

Error BinaryConverter_WriteInt8(BinaryConverter* self, GenericBuffer* destination, int8_t value);

Error BinaryConverter_WriteUInt8(BinaryConverter* self, GenericBuffer* destination, uint8_t value);

Error BinaryConverter_WriteInt16(BinaryConverter* self, GenericBuffer* destination, int16_t value);

Error BinaryConverter_WriteUInt16(BinaryConverter* self, GenericBuffer* destination, uint16_t value);

Error BinaryConverter_WriteInt32(BinaryConverter* self, GenericBuffer* destination, int32_t value);

Error BinaryConverter_WriteUInt32(BinaryConverter* self, GenericBuffer* destination, uint32_t value);

Error BinaryConverter_WriteInt64(BinaryConverter* self, GenericBuffer* destination, int64_t value);

Error BinaryConverter_WriteUInt64(BinaryConverter* self, GenericBuffer* destination, uint64_t value);

Error BinaryConverter_WriteFloat(BinaryConverter* self, GenericBuffer* destination, float value);

Error BinaryConverter_WriteDouble(BinaryConverter* self, GenericBuffer* destination, double value);

/* Writes only 0 or 1. */
Error BinaryConverter_WriteBoolean(BinaryConverter* self, GenericBuffer* destination, bool value);

Error BinaryConverter_EncodeInt32(BinaryConverter* self, GenericBuffer* destination, int32_t value);

Error BinaryConverter_EncodeUInt32(BinaryConverter* self, GenericBuffer* destination, uint32_t value);

Error BinaryConverter_EncodeInt64(BinaryConverter* self, GenericBuffer* destination, int64_t value);

Error BinaryConverter_EncodeUInt64(BinaryConverter* self, GenericBuffer* destination, uint64_t value);

Error BinaryConverter_ReadInt8(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int8_t* value);

Error BinaryConverter_ReadUInt8(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint8_t* value);

Error BinaryConverter_ReadInt16(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int16_t* value);

Error BinaryConverter_ReadUInt16(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint16_t* value);

Error BinaryConverter_ReadInt32(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int32_t* value);

Error BinaryConverter_ReadUInt32(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint32_t* value);

Error BinaryConverter_ReadInt64(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, int64_t* value);

Error BinaryConverter_ReadUInt64(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, uint64_t* value);

Error BinaryConverter_ReadFloat(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, float* value);

Error BinaryConverter_ReadDouble(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, double* value);

/* Only values of 0 or 1 are valid. */
Error BinaryConverter_ReadBoolean(BinaryConverter* self, unsigned char* source, size_t maxSourceLength, bool* value);

Error BinaryConverter_DecodeInt32(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    int32_t* value,
    size_t* outBytesRead);

Error BinaryConverter_DecodeUInt32(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    uint32_t* value,
    size_t* outBytesRead);

Error BinaryConverter_DecodeInt64(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    int64_t* value,
    size_t* outBytesRead);

Error BinaryConverter_DecodeUInt64(BinaryConverter* self,
    unsigned char* source,
    size_t maxSourceLength,
    uint64_t* value,
    size_t* outBytesRead);

void BinaryConverter_Deconstruct(BinaryConverter* self);



Error BinaryIOStream_Construct1(BinaryIOStream* self, IOStream* wrappedStream, bool ownsWrappedStream);

Error BinaryIOStream_Construct2(BinaryIOStream* self, IOStream* wrappedStream, MachineEndianess targetEndianness, bool ownsWrappedStream);

static inline Error BinaryIOStream_SetTargetEndianness(BinaryIOStream* self, MachineEndianess targetEndianness)
{
    return BinaryConverter_SetTargetEndianness(&self->_converter, targetEndianness);
}

Error BinaryIOStream_WriteInt8(BinaryIOStream* self, int8_t value);

Error BinaryIOStream_WriteUInt8(BinaryIOStream* self, uint8_t value);

Error BinaryIOStream_WriteInt16(BinaryIOStream* self, int16_t value);

Error BinaryIOStream_WriteUInt16(BinaryIOStream* self, uint16_t value);

Error BinaryIOStream_WriteInt32(BinaryIOStream* self, int32_t value);

Error BinaryIOStream_WriteUInt32(BinaryIOStream* self, uint32_t value);

Error BinaryIOStream_WriteInt64(BinaryIOStream* self, int64_t value);

Error BinaryIOStream_WriteUInt64(BinaryIOStream* self, uint64_t value);

Error BinaryIOStream_WriteFloat(BinaryIOStream* self, float value);

Error BinaryIOStream_WriteDouble(BinaryIOStream* self, double value);

Error BinaryIOStream_WriteBoolean(BinaryIOStream* self, bool value);

Error BinaryIOStream_WriteEncodedInt32(BinaryIOStream* self, int32_t value);

Error BinaryIOStream_WriteEncodedUInt32(BinaryIOStream* self, uint32_t value);

Error BinaryIOStream_WriteEncodedInt64(BinaryIOStream* self, int64_t value);

Error BinaryIOStream_WriteEncodedUInt64(BinaryIOStream* self, uint64_t value);

Error BinaryIOStream_ReadInt8(BinaryIOStream* self, int8_t* value);

Error BinaryIOStream_ReadUInt8(BinaryIOStream* self, uint8_t* value);

Error BinaryIOStream_ReadInt16(BinaryIOStream* self, int16_t* value);

Error BinaryIOStream_ReadUInt16(BinaryIOStream* self, uint16_t* value);

Error BinaryIOStream_ReadInt32(BinaryIOStream* self, int32_t* value);

Error BinaryIOStream_ReadUInt32(BinaryIOStream* self, uint32_t* value);

Error BinaryIOStream_ReadInt64(BinaryIOStream* self, int64_t* value);

Error BinaryIOStream_ReadUInt64(BinaryIOStream* self, uint64_t* value);

Error BinaryIOStream_ReadFloat(BinaryIOStream* self, float* value);

Error BinaryIOStream_ReadDouble(BinaryIOStream* self, double* value);

Error BinaryIOStream_ReadBoolean(BinaryIOStream* self, bool* value);

Error BinaryIOStream_ReadEncodedInt32(BinaryIOStream* self, int32_t* value);

Error BinaryIOStream_ReadEncodedUInt32(BinaryIOStream* self, uint32_t* value);

Error BinaryIOStream_ReadEncodedInt64(BinaryIOStream* self, int64_t* value);

Error BinaryIOStream_ReadEncodedUInt64(BinaryIOStream* self, uint64_t* value);

Error BinaryIOStream_Deconstruct(BinaryIOStream* self);
