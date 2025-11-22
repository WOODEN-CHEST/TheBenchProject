#pragma once


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