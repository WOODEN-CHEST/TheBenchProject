#pragma once
#include <stdint.h>
#include <stddef.h>



// Fields.

/**
* Maximum length of a single error message buffer, includes the null terminator.
* If the formatted or plain message passed to an error construct function is longer than this size,
* then it gets truncated. The resulting message, truncated or not, is always null-terminated.
*/
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
 * The message in the returned error, if not null, is owned by the error pool and should not be freed explicitly, use
 * the error pool's clear function to control the message's lifetime.
 */
typedef struct ErrorStruct
{
    ErrorCode Code;
    const unsigned char* Message;
} Error;


/**
 * An error message pool holds error message buffers.
 */
typedef struct ErrorMessagePoolStruct
{
    unsigned char* _messages;
    size_t _count;
    size_t _capacity;
} ErrorMessagePool;


// Functions.

/**
 * Creates an error which indicates success and has no message.
 */
Error Error_CreateSuccess(void);

/**
 * Creates a non-success error.
 * @param pool The error pool from which to pull the error message buffer, may be null to not create an error message.
 * @param code The error code, mustn't be the success code.
 * @param message The message to use for the error, may be null if the error pool is null.
 */
Error Error_Construct1(ErrorMessagePool* pool, ErrorCode code, const unsigned char* message);

/**
 * Creates a non-success error.
 * @param pool The error pool from which to pull the error message buffer, may be null to not create an error message.
 * @param code The error code, mustn't be the success code.
 * @param message The message to use for the error, may be null if the error pool is null.
 */
Error Error_Construct2(ErrorMessagePool* pool, ErrorCode code, char* message);

/**
 * Creates a non-success error by formatting the given message.
 * @param pool The error pool from which to pull the error message buffer, may be null to not create an error message.
 * @param code The error code, mustn't be the success code.
 * @param format The message which to format into an error message, printf style. May be null if the error pool is null.
 */
Error Error_Construct3(ErrorMessagePool* pool, ErrorCode code, const unsigned char* format, ...);

/**
 * Creates a non-success error by formatting the given message.
 * @param pool The error pool from which to pull the error message buffer, may be null to not create an error message.
 * @param code The error code, mustn't be the success code.
 * @param format The message which to format into an error message, printf style. May be null if the error pool is null.
 */
Error Error_Construct4(ErrorMessagePool* pool, ErrorCode code, char* format, ...);

/**
 * Creates a non-success error without a message. No error message pool is required since a message isn't being created.
 * @param code The error code, mustn't be the success code.
 */
Error Error_Construct5(ErrorCode code);


/**
 * Creates an error message pool and initializes it to the internal default non-zero capacity in message buffer count.
 */
void ErrorMessagePool_Construct1(ErrorMessagePool* self);

/**
 * Deconstructs the given error message pool and releases all memory used by it.
 * All references to error messages owned by this pool become invalid after a call to this function.
 */
void ErrorMessagePool_Deconstruct1(ErrorMessagePool* self);

/**
 * Clears the error pool from any messages, giving back space for future messages.
 * All references to error messages in this pool become invalid after a call to this function.
 * This function is not expensive, it can be called in a tight loop if required.
 */
void ErrorMessagePool_Clear(ErrorMessagePool* self);

/**
 * Gets a pointer to the next available error message buffer in this pool.
 * If the pool is full, then more memory is allocated for it.
 * This method always returns a valid error message buffer.
 */
unsigned char* ErrorMessagePool_GetNextMessage(ErrorMessagePool* self);