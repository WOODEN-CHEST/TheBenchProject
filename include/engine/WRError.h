#pragma once
#include <stdint.h>
#include <stddef.h>



// Fields.
#define MAX_ERROR_MESSAGE_BUFFER_LENGTH 8192


// Types.
typedef enum ErrorCodeEnum
{
    ErrorCode_Success = 0,

    ErrorCode_IllegalArgument,
    ErrorCode_ArgumentOutOfRange,

    ErrorCode_InvalidOperation,
    ErrorCode_InvalidState,

    ErrorCode_Initialization,
    ErrorCode_Deinitialization,

    ErrorCode_Construct,
    ErrorCode_Deconstruct,

    ErrorCode_IndexOutOfBounds,

    ErrorCode_IO,
    ErrorCode_FileNotFound,
    ErrorCode_DirectoryNotFound,
    ErrorCode_InvalidPath,

    ErrorCode_Serialize,
    ErrorCode_Deserialize,
    ErrorCode_EncodeError,
    ErrorCode_DecodeError,

    ErrorCode_InvalidJSON,

    ErrorCode_InvalidAssetDefinition,
    ErrorCode_InvalidAssetData,

    ErrorCode_InvalidUnicodeData,

    ErrorCode_BufferTooSmall,

    ErrorCode_Unknown
    
} ErrorCode;

typedef struct ErrorStruct
{
    ErrorCode Code;
    const unsigned char* Message;
} Error;

typedef struct ErrorMessagePoolStruct
{
    unsigned char* _messages;
    size_t _count;
    size_t _capacity;
} ErrorMessagePool;


// Functions.
Error Error_CreateSuccess(void);

Error Error_Construct(ErrorMessagePool* pool, ErrorCode code, const unsigned char* message);

Error Error_Construct2(ErrorMessagePool* pool, ErrorCode code, char* message);

Error Error_Construct3(ErrorMessagePool* pool, ErrorCode code, const unsigned char* format, ...);

Error Error_Construct4(ErrorMessagePool* pool, ErrorCode code, char* format, ...);

Error Error_Construct5(ErrorCode code);

void ErrorMessagePool_Construct1(ErrorMessagePool* self);

void ErrorMessagePool_Deconstruct1(ErrorMessagePool* self);

void ErrorMessagePool_Clear(ErrorMessagePool* self);

unsigned char* ErrorMessagePool_GetNextMessage(ErrorMessagePool* self);