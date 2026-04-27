#include "WRHash.h"


// Macros.
#define HASH_FNV1A_OFFSET_BASIS UINT64_C(14695981039346656037)
#define HASH_FNV1A_PRIME UINT64_C(1099511628211)


// Static functions.
static size_t GetStringByteCount(const unsigned char* str)
{
    size_t Length = 0;

    if (str == NULL)
    {
        return 0;
    }

    while (str[Length] != u8'\0')
    {
        Length++;
    }

    return Length;
}


// Public functions.
HashCode Hash_String(const unsigned char* str)
{
    return Hash_HashBytes(str, GetStringByteCount(str));
}

HashCode Hash_Pointer(const void* pointer)
{
    return Hash_HashBytes((const unsigned char*)&pointer, sizeof(pointer));
}

HashCode Hash_HashBytes(const unsigned char* bytes, size_t byteCount)
{
    HashCode Result = HASH_FNV1A_OFFSET_BASIS;

    if ((bytes == NULL) && (byteCount > 0))
    {
        return Result;
    }

    for (size_t Index = 0; Index < byteCount; Index++)
    {
        Result ^= bytes[Index];
        Result *= HASH_FNV1A_PRIME;
    }

    return Result;
}

HashCode Hash_Int8(int8_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_Int16(int16_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_Int32(int32_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_Int64(int64_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_UInt8(uint8_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_UInt16(uint16_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_UInt32(uint32_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_UInt64(uint64_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_SizeT(size_t value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_Float(float value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}

HashCode Hash_Double(double value)
{
    return Hash_HashBytes((const unsigned char*)&value, sizeof(value));
}
