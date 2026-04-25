#include "WREnvironment.h"
#include <stdint.h>



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