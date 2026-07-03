#pragma once
#include "WRUnicode.h"


// Types.
/**
 * @brief Byte order of the host machine.
 *
 * Identifies whether multi-byte scalars are stored least-significant-byte-first
 * (little-endian) or most-significant-byte-first (big-endian). Returned by
 * Environment_GetEndianess.
 */
typedef enum MachineEndianessEnum
{
    /** @brief Least-significant byte is stored at the lowest address. */
    MachineEndianess_LittleEndian,
    /** @brief Most-significant byte is stored at the lowest address. */
    MachineEndianess_BigEndian
} MachineEndianess;


// Fields.
/**
 * @brief The host platform's line terminator as a NUL-terminated UTF-8 string.
 *
 * "\n" on Linux and "\r\n" on Windows. The pointer and the bytes it references are static and
 * read-only (borrowed, never freed) and remain valid for the program's lifetime.
 * @note On the supported platforms this name is also provided as a compile-time string-literal
 *       macro (see below), so it can be used in contexts that require a constant expression.
 */
extern const unsigned char* const ENVIRONMENT_NEWLINE_STRING;

/**
 * @brief The host platform's primary path component separator.
 *
 * '/' on Linux, '\\' on Windows. This is the separator the framework emits when building paths.
 * @note On the supported platforms this name is also provided as a compile-time macro (see below).
 */
extern const unsigned char ENVIRONMENT_PATH_SEPARATOR_PRIMARY;

/**
 * @brief The host platform's secondary (alternative) path component separator.
 *
 * Equal to the primary separator on Linux; on Windows it is '/', which Windows also accepts as a
 * separator. Used when recognizing separators in input paths.
 * @note On the supported platforms this name is also provided as a compile-time macro (see below).
 */
extern const unsigned char ENVIRONMENT_PATH_SEPARATOR_SECONDARY;


// Functions.
/**
 * @brief Determines the byte order of the host machine at run time.
 *
 * Detects endianness by inspecting the in-memory representation of a known integer. Pure: it has
 * no parameters, no side effects, and always reflects the machine it runs on.
 * @returns MachineEndianess_LittleEndian or MachineEndianess_BigEndian for the current machine.
 */
MachineEndianess Environment_GetEndianess(void);


// OS specific.
#if defined __linux__

#define ENVIRONMENT_NEWLINE_STRING "\n"
#define ENVIRONMENT_PATH_SEPARATOR_PRIMARY ((CodePoint)'/')
#define ENVIRONMENT_PATH_SEPARATOR_SECONDARY ENVIRONMENT_PATH_SEPARATOR_PRIMARY

#elif defined _WIN32

#define ENVIRONMENT_NEWLINE_STRING "\r\n"
#define ENVIRONMENT_PATH_SEPARATOR_PRIMARY ((CodePoint)'\\')
#define ENVIRONMENT_PATH_SEPARATOR_SECONDARY ((CodePoint)'/')

#endif
