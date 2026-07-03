#pragma once
#include <stddef.h>
#include <stdint.h>


/**
 * UserData is a fixed 128-byte value used wherever the framework lets a caller attach their
 * own data to a callback, a subscription, or a pooled object.
 *
 * It exists so the caller can choose how to pass their context:
 *   - Store a single pointer (UserData_FromPointer / UserData_GetPointer) when the real data
 *     lives elsewhere and must outlive the call.
 *   - Store a small value inline (write to .Bytes, or through a typed view) to avoid a separate
 *     allocation when the data is small enough to fit in the inline storage.
 *
 * Passing and lifetime rules (also documented in AGENTS.md):
 *   - Never pass UserData by value as a function argument. 128 bytes on the stack per call is
 *     wasteful. Functions that use the data immediately (predicates, comparators, mappers,
 *     extractors, and similar helpers) take a 'const UserData*' instead.
 *   - When the data must be retained past the call (event subscriptions, pooled objects, threads),
 *     the owning struct stores a UserData by value (a copy) and hands a pointer to that stored
 *     copy to the callback when it fires.
 *
 * UserData owns nothing. Copying it is a plain byte copy and there is no destructor. If a pointer
 * is stored inside it, ownership of the pointee is unchanged and is the caller's responsibility.
 */


#define USER_DATA_BYTE_COUNT ((size_t)128)


// Types.
/**
 * @brief A fixed 128-byte caller-attached value with overlapping views of its storage.
 *
 * A union providing several ways to read/write the same inline storage: as a raw byte array, as a
 * single pointer, or (for the alignment member) as suitably aligned scratch. Owns nothing; copying
 * is a plain byte copy and there is no destructor. See the file-level comment for passing/lifetime rules.
 */
typedef union UserDataUnion
{
    /** @brief The inline storage viewed as raw bytes; write here to store a small value inline. */
    unsigned char Bytes[USER_DATA_BYTE_COUNT];
    /** @brief The storage viewed as a single pointer; use when the real data lives elsewhere. */
    void* Pointer;
    max_align_t _alignment; // Forces alignment suitable for any value stored inline; never read directly.
} UserData;


// Functions.

/**
 * @brief Returns a zero-initialized UserData.
 *
 * All bytes are zero, which also means the pointer view is NULL.
 * @returns A UserData whose storage is entirely zeroed.
 */
static inline UserData UserData_CreateEmpty(void)
{
    return (UserData){ 0 };
}

/**
 * @brief Returns a UserData that holds a single pointer.
 *
 * Stores the pointer in the pointer view; all remaining bytes are zero. Ownership of the pointee
 * is unchanged and remains the caller's responsibility.
 * @param pointer The pointer to store. May be NULL.
 * @returns A UserData whose pointer view is the given pointer and whose other bytes are zero.
 */
static inline UserData UserData_FromPointer(void* pointer)
{
    UserData Data = { 0 };
    Data.Pointer = pointer;
    return Data;
}

/**
 * @brief Returns the pointer stored in the UserData.
 *
 * Reads the pointer view of the storage. Only meaningful if the value was created/written as a
 * pointer (e.g. via UserData_FromPointer); reading it after storing unrelated inline bytes yields
 * an unspecified pointer.
 * @param self The UserData to read. Must not be NULL.
 * @returns The pointer stored in the UserData's pointer view.
 */
static inline void* UserData_GetPointer(const UserData* self)
{
    return self->Pointer;
}
