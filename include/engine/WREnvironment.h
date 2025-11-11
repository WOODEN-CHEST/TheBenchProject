#pragma once


// Types.
typedef enum MachineEndianessEnum
{
    MachineEndianess_LittleEndian,
    MachineEndianess_BigEndian
} MachineEndianess;


// Fields.
extern const unsigned char* const ENVIRONMENT_NEWLINE_STRING;


// Functions.
MachineEndianess Environment_GetEndianess(void);