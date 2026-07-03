#pragma once
#include <stdint.h>
#include <stddef.h>


/**
 * Error module provides exception like errors and their handling functions.
 * Errors are typically returned from functions by their return value rather than writing to a pointer which
 * points to an error object.
 */


// Types.
/**
 * @brief Categorizes the kind of failure carried by an Error.
 *
 * ErrorCode_Success (value 0) means "no error"; every other constant denotes a specific
 * failure category. Code 0 is reserved for success, so it can be tested as a boolean (zero =
 * success, non-zero = failure).
 */
typedef enum ErrorCodeEnum
{
    /** @brief No error occurred; the operation succeeded. The accompanying message is NULL. */
    ErrorCode_Success = 0,

    /** @brief A function argument was invalid (wrong value, type, or NULL where not allowed). */
    ErrorCode_IllegalArgument,
    /** @brief A numeric or index argument fell outside its accepted range. */
    ErrorCode_ArgumentOutOfRange,

    /** @brief The operation is not valid given the current circumstances. */
    ErrorCode_InvalidOperation,
    /** @brief The target object is in a state in which the operation cannot proceed. */
    ErrorCode_InvalidState,

    /** @brief A module or subsystem failed to initialize. */
    ErrorCode_Initialization,
    /** @brief A module or subsystem failed to deinitialize/shut down cleanly. */
    ErrorCode_Deinitialization,

    /** @brief An object failed to construct. */
    ErrorCode_Construct,
    /** @brief An object failed to deconstruct/release cleanly. */
    ErrorCode_Deconstruct,

    /** @brief An index was outside the bounds of the collection or buffer being accessed. */
    ErrorCode_IndexOutOfBounds,

    /** @brief A general input/output failure. */
    ErrorCode_IO,
    /** @brief The requested file does not exist. */
    ErrorCode_FileNotFound,
    /** @brief The requested directory does not exist. */
    ErrorCode_DirectoryNotFound,
    /** @brief A filesystem path was malformed or otherwise invalid. */
    ErrorCode_InvalidPath,

    /** @brief Serialization of a value into its external representation failed. */
    ErrorCode_Serialize,
    /** @brief Deserialization of a value from its external representation failed. */
    ErrorCode_Deserialize,
    /** @brief Encoding data into a target format failed. */
    ErrorCode_EncodeError,
    /** @brief Decoding data from a source format failed. */
    ErrorCode_DecodeError,

    /** @brief Input was not valid JSON. */
    ErrorCode_InvalidJSON,

    /** @brief An asset's definition/metadata was invalid. */
    ErrorCode_InvalidAssetDefinition,
    /** @brief An asset's binary/content data was invalid. */
    ErrorCode_InvalidAssetData,

    /** @brief Data was not valid Unicode. */
    ErrorCode_InvalidUnicodeData,
    /** @brief A codepoint value was outside the valid Unicode range or otherwise invalid. */
    ErrorCode_InvalidCodePoint,
    /** @brief A text encoding was unsupported or malformed. */
    ErrorCode_InvalidTextEncoding,

    /** @brief A destination buffer was too small to hold the result. */
    ErrorCode_BufferTooSmall,
    /** @brief A buffer exceeded the maximum permitted size. */
    ErrorCode_BufferTooLarge,

    /** @brief An unspecified or unclassified error. */
    ErrorCode_Unknown

} ErrorCode;

/**
 * @brief An error value returned from any function that may fail.
 *
 * A success code (ErrorCode_Success) indicates no failure, and its message is NULL. A failure is
 * indicated by a non-success code together with an OPTIONAL error message (the message may still
 * be NULL). If the message is non-NULL it is owned by the error instance and must be released
 * with Error_Deconstruct.
 */
typedef struct ErrorStruct
{
    /** @brief The error category; ErrorCode_Success means no failure. */
    ErrorCode Code;
    /** @brief Optional owned, NUL-terminated UTF-8 message; NULL when absent. Release with Error_Deconstruct. */
    const unsigned char* Message;
} Error;


// Functions.

/**
 * @brief Creates an error that indicates success and carries no message.
 * @returns An Error with Code == ErrorCode_Success and a NULL Message. No release is required.
 */
Error Error_CreateSuccess(void);

/**
 * @brief Creates a non-success error from a UTF-8 message.
 *
 * If a message is supplied it is copied into a heap-allocated, error-owned message that must be
 * released with Error_Deconstruct.
 * @param code The error code; must not be ErrorCode_Success.
 * @param message The message to use for the error, as a NUL-terminated UTF-8 string. May be NULL
 *        to create an error without a message.
 * @returns An Error carrying the given code and (optionally) an owned copy of the message.
 */
Error Error_Construct1(ErrorCode code, const unsigned char* message);

/**
 * @brief Creates a non-success error from a plain char message.
 *
 * If a message is supplied it is copied into a heap-allocated, error-owned message that must be
 * released with Error_Deconstruct.
 * @param code The error code; must not be ErrorCode_Success.
 * @param message The message to use for the error, as a NUL-terminated string. May be NULL to
 *        create an error without a message.
 * @returns An Error carrying the given code and (optionally) an owned copy of the message.
 */
Error Error_Construct2(ErrorCode code, const char* message);

/**
 * @brief Creates a non-success error by formatting a UTF-8 message, printf style.
 *
 * The formatted result is stored in a heap-allocated, error-owned message that must be released
 * with Error_Deconstruct.
 * @param code The error code; must not be ErrorCode_Success.
 * @param format The printf-style format string (UTF-8) to format into the error message. May be
 *        NULL to create an error without a message.
 * @param ... Arguments consumed by the format string.
 * @returns An Error carrying the given code and (optionally) an owned formatted message.
 */
Error Error_Construct3(ErrorCode code, const unsigned char* format, ...);

/**
 * @brief Creates a non-success error by formatting a plain char message, printf style.
 *
 * The formatted result is stored in a heap-allocated, error-owned message that must be released
 * with Error_Deconstruct.
 * @param code The error code; must not be ErrorCode_Success.
 * @param format The printf-style format string to format into the error message. May be NULL to
 *        create an error without a message.
 * @param ... Arguments consumed by the format string.
 * @returns An Error carrying the given code and (optionally) an owned formatted message.
 */
Error Error_Construct4(ErrorCode code, const char* format, ...);

/**
 * @brief Creates a non-success error with no message.
 * @param code The error code; must not be ErrorCode_Success.
 * @returns An Error carrying the given code and a NULL Message.
 */
Error Error_Construct5(ErrorCode code);

/**
 * @brief Releases any owned error message and resets the error to success.
 *
 * Frees the owned message (if any), then sets the error to the success state (ErrorCode_Success,
 * NULL message). Safe to call on a success error or one with no message, and safe to call more
 * than once on the same Error (idempotent).
 * @param self The error to release and reset. Must not be NULL.
 */
void Error_Deconstruct(Error* self);
