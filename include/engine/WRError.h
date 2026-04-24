#pragma once
#include <stdint.h>
#include <stddef.h>


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
    ErrorCode_InvalidCodePoint,
    ErrorCode_InvalidTextEncoding,

    ErrorCode_BufferTooSmall,
    ErrorCode_BufferTooLarge,

    ErrorCode_Unknown
    
} ErrorCode;

/**
 * An error struct returned from any functions which may fail.
 * 
 * A success code may indicate no failure, and the error message should be null.
 * A failure may be indicated by a non-success code and OPTIONAL error message (it may still be null).
 * The message in the returned error, if not null, is owned by the error instance and must be released with
 * Error_Deconstruct.
 */
typedef struct ErrorStruct
{
    ErrorCode Code;
    const unsigned char* Message;
} Error;


// Functions.

/**
 * Creates an error which indicates success and has no message.
 */
Error Error_CreateSuccess(void);

/**
 * Creates a non-success error.
 * @param code The error code, mustn't be the success code.
 * @param message The message to use for the error, may be null to create an error without a message.
 */
Error Error_Construct1(ErrorCode code, const unsigned char* message);

/**
 * Creates a non-success error.
 * @param code The error code, mustn't be the success code.
 * @param message The message to use for the error, may be null to create an error without a message.
 */
Error Error_Construct2(ErrorCode code, const char* message);

/**
 * Creates a non-success error by formatting the given message.
 * @param code The error code, mustn't be the success code.
 * @param format The message which to format into an error message, printf style. May be null to create an error without a message.
 */
Error Error_Construct3(ErrorCode code, const unsigned char* format, ...);

/**
 * Creates a non-success error by formatting the given message.
 * @param code The error code, mustn't be the success code.
 * @param format The message which to format into an error message, printf style. May be null to create an error without a message.
 */
Error Error_Construct4(ErrorCode code, const char* format, ...);

/**
 * Creates a non-success error without a message.
 * @param code The error code, mustn't be the success code.
 */
Error Error_Construct5(ErrorCode code);

/**
 * Releases any owned error message and resets the error to success.
 */
void Error_Deconstruct(Error* self);
