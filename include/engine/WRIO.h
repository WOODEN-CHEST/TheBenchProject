#pragma once
#include <stddef.h>
#include <stdint.h>
#include <WRMemory.h>
#include "WRError.h"



// Types.
typedef enum IOStreamTypeEnum
{
    IOStreamType_File,
    IOStreamType_Memory,
    
} IOStreamType;

typedef struct IOStreamStruct
{
    IOStreamType _type;
    void* _data;
} IOStream;

typedef enum IOStreamSeekOriginEnum
{
    IOStreamSeekOrigin_Start,
    IOStreamSeekOrigin_Current,
    IOStreamSeekOrigin_End,
} IOStreamSeekOrigin;


// Functions.
IOStream IOStream_GetFromStandardInput(void);

IOStream IOStream_GetFromStandardOutput(void);

IOStream IOStream_GetFromStandardError(void);

Error IOSteam_GetPosition(ErrorMessagePool* errorPool, IOStream* stream, size_t* position);

Error IOSteam_SetPosition(ErrorMessagePool* errorPool, IOStream* stream, size_t position);

Error IOSteam_SetPositionSpecial(ErrorMessagePool* errorPool, IOStream* stream, IOStreamSeekOrigin origin);

Error IOStream_GetStreamSize(ErrorMessagePool* errorPool, IOStream* stream, size_t* sizeBytes);

Error IOStream_GetStreamSizeRemaining(ErrorMessagePool* errorPool, IOStream* stream, size_t* sizeBytes);

void IOStream_Flush(IOStream* stream);

void IOStream_WriteByte(IOStream* stream, unsigned char byte);

void IOStream_Write(IOStream* stream, const unsigned char* data, size_t dataSize);

void IOStream_WriteString(IOStream* stream, const unsigned char* str);

void IOStream_WriteFormattedString(IOStream* stream, const unsigned char* format, ...);

Error IOStream_ReadByte(ErrorMessagePool* errorPool, IOStream* stream, unsigned char* byte);

Error IOStream_Read(ErrorMessagePool* errorPool, IOStream* stream, size_t bytesToRead, GenericBuffer* outBuffer);

bool IOStream_IsEndOfFile(IOStream* stream);

void IOStream_Close(IOStream stream);