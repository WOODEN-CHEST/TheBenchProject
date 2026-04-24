#pragma once
#include "WRUnicode.h"


// Types.
typedef enum MachineEndianessEnum
{
    MachineEndianess_LittleEndian,
    MachineEndianess_BigEndian
} MachineEndianess;


// Fields.
extern const unsigned char* const ENVIRONMENT_NEWLINE_STRING;
extern const unsigned char ENVIRONMENT_PATH_SEPARATOR_PRIMARY;
extern const unsigned char ENVIRONMENT_PATH_SEPARATOR_SECONDARY;


// Functions.
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