#include "WREnvironment.h"
#include <stdint.h>


// Fields.
#ifdef __unix__
const unsigned char* const ENVIRONMENT_NEWLINE_STRING = u8"\n";
const unsigned char ENVIRONMENT_PATH_SEPARATOR_PRIMARY = '/';
const unsigned char ENVIRONMENT_PATH_SEPARATOR_SECONDARY = '/';
#elif __WIN32
const unsigned char* const ENVIRONMENT_NEWLINE_STRING = u8"\r\n";
const unsigned char ENVIRONMENT_PATH_SEPARATOR_PRIMARY = '\\';
const unsigned char ENVIRONMENT_PATH_SEPARATOR_SECONDARY = '/';
#endif


// Types.
typedef union EndianessCheckUnion
{
    int32_t Int32;
    unsigned char Bytes[sizeof(int32_t)];
} EndianessCheck;



// Functions.
MachineEndianess Environment_GetEndianess()
{
    EndianessCheck Value;
    Value.Int32 = 1;

    return Value.Bytes[0] ? MachineEndianess_LittleEndian : MachineEndianess_BigEndian;
}