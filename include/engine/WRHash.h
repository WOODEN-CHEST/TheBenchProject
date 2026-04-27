#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint64_t HashCode;


HashCode Hash_String(const unsigned char* str);

HashCode Hash_Pointer(const void* pointer);

HashCode Hash_HashBytes(const unsigned char* bytes, size_t byteCount);

HashCode Hash_Int8(int8_t value);

HashCode Hash_Int16(int16_t value);

HashCode Hash_Int32(int32_t value);

HashCode Hash_Int64(int64_t value);

HashCode Hash_UInt8(uint8_t value);

HashCode Hash_UInt16(uint16_t value);

HashCode Hash_UInt32(uint32_t value);

HashCode Hash_UInt64(uint64_t value);

HashCode Hash_SizeT(size_t value);

HashCode Hash_Float(float value);

HashCode Hash_Double(double value);