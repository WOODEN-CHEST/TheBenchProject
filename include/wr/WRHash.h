#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Unsigned 64-bit hash code produced by the Hash_* functions.
 *
 * The value is a non-cryptographic hash (FNV-1a). It is suitable for hash tables and
 * similar in-memory use, but must NOT be relied on for security, integrity, or as a
 * stable persisted/wire value: it is not collision-resistant and the algorithm/result
 * may differ across platforms or framework versions.
 */
typedef uint64_t HashCode;


/**
 * @brief Computes the hash of a NUL-terminated UTF-8 string.
 *
 * Hashes the bytes of the string up to (but not including) the terminating '\0'. A NULL
 * pointer is treated as an empty string and yields the same hash as a zero-length input.
 * The result is a non-cryptographic FNV-1a hash.
 * @param str The NUL-terminated byte string to hash. May be NULL (treated as empty).
 * @returns The 64-bit hash of the string's bytes.
 */
HashCode Hash_String(const unsigned char* str);

/**
 * @brief Computes a hash from the value of a pointer.
 *
 * Hashes the raw bytes of the pointer value itself, not the memory it points to (the
 * pointee is never dereferenced and may be NULL or dangling). Two equal pointer values
 * hash equally; because it hashes the object representation, the result depends on the
 * platform's pointer size and byte order. The result is a non-cryptographic FNV-1a hash.
 * @param pointer The pointer value to hash. Not dereferenced; any value (including NULL) is allowed.
 * @returns The 64-bit hash of the pointer's bit pattern.
 */
HashCode Hash_Pointer(const void* pointer);

/**
 * @brief Computes a hash over an arbitrary byte buffer.
 *
 * Combines the first byteCount bytes of the buffer into a non-cryptographic FNV-1a hash.
 * Passing byteCount 0 yields the FNV-1a offset basis regardless of the pointer. As a
 * safety guard, if bytes is NULL while byteCount is greater than 0 the function does not
 * dereference the pointer and instead returns that same offset-basis value (so a misuse
 * does not crash, but does not produce a meaningful content hash either).
 * @param bytes Pointer to the bytes to hash. May be NULL only when byteCount is 0.
 * @param byteCount Number of bytes to read from bytes.
 * @returns The 64-bit hash of the byte range.
 */
HashCode Hash_HashBytes(const unsigned char* bytes, size_t byteCount);

/**
 * @brief Computes a hash of an 8-bit signed integer.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a).
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_Int8(int8_t value);

/**
 * @brief Computes a hash of a 16-bit signed integer.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a); the
 * result therefore depends on the platform's byte order.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_Int16(int16_t value);

/**
 * @brief Computes a hash of a 32-bit signed integer.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a); the
 * result therefore depends on the platform's byte order.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_Int32(int32_t value);

/**
 * @brief Computes a hash of a 64-bit signed integer.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a); the
 * result therefore depends on the platform's byte order.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_Int64(int64_t value);

/**
 * @brief Computes a hash of an 8-bit unsigned integer.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a).
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_UInt8(uint8_t value);

/**
 * @brief Computes a hash of a 16-bit unsigned integer.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a); the
 * result therefore depends on the platform's byte order.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_UInt16(uint16_t value);

/**
 * @brief Computes a hash of a 32-bit unsigned integer.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a); the
 * result therefore depends on the platform's byte order.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_UInt32(uint32_t value);

/**
 * @brief Computes a hash of a 64-bit unsigned integer.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a); the
 * result therefore depends on the platform's byte order.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_UInt64(uint64_t value);

/**
 * @brief Computes a hash of a size_t value.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a); because
 * size_t width varies, the result depends on the platform's pointer/size width and byte order.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_SizeT(size_t value);

/**
 * @brief Computes a hash of a single-precision float.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a). Because it
 * hashes the bit pattern, distinct encodings of equal numbers do not collapse: +0.0 and -0.0
 * hash differently, and distinct NaN bit patterns hash differently.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_Float(float value);

/**
 * @brief Computes a hash of a double-precision float.
 *
 * Hashes the raw object representation of the value (non-cryptographic FNV-1a). Because it
 * hashes the bit pattern, distinct encodings of equal numbers do not collapse: +0.0 and -0.0
 * hash differently, and distinct NaN bit patterns hash differently.
 * @param value The value to hash.
 * @returns The 64-bit hash of the value's bytes.
 */
HashCode Hash_Double(double value);
