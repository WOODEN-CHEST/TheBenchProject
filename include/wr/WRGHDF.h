#pragma once
#include "WRCollection.h"
#include "WRError.h"
#include "WRIO.h"
#include "WRMemory.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/**
 * @brief Reserved entry ID that is never a valid key in a GHDF compound.
 *
 * Compound entries are keyed by a 64-bit ID; the value 0 is rejected by every operation
 * that accepts an ID (it raises ErrorCode_IllegalArgument). Use this constant to denote
 * "no entry"/"unset ID".
 */
#define GHDF_ENTRY_ID_INVALID ((GHDFEntryID)0)


// Types.
/**
 * @brief Unsigned 64-bit key identifying an entry within a GHDF compound.
 *
 * IDs are caller-assigned. The value GHDF_ENTRY_ID_INVALID (0) is reserved and rejected.
 */
typedef uint64_t GHDFEntryID;

/**
 * @brief Opaque keyed container of typed entries (the GHDF analogue of an object/map).
 *
 * A compound maps GHDFEntryID keys to typed GHDFObjectValue values. Compounds are not
 * constructed directly: they are obtained from a GHDFObjectPool (see
 * GHDFObjectPool_BorrowCompound) and returned to it. Nested compounds, arrays, and strings
 * stored inside a compound must likewise be pool-borrowed.
 */
typedef struct GHDFCompoundStruct GHDFCompound;

/**
 * @brief Opaque homogeneous, ordered list of typed values, all of one GHDFValueType.
 *
 * Every element shares the array's element type (which must be a concrete scalar/string/compound
 * type, never an array — GHDF arrays cannot nest arrays). Arrays are obtained from and returned
 * to a GHDFObjectPool (see GHDFObjectPool_BorrowArray).
 */
typedef struct GHDFArrayStruct GHDFArray;

/**
 * @brief Opaque pool that owns and recycles GHDFCompound, GHDFArray, and string buffers.
 *
 * The pool is the sole producer of compounds, arrays, and GHDF string buffers; all such objects
 * carry a hidden token identifying their owning pool. Borrowed objects must be returned to the
 * same pool. Created with GHDFObjectPool_Create and destroyed with GHDFObjectPool_Deconstruct.
 */
typedef struct GHDFObjectPoolStruct GHDFObjectPool;

/**
 * @brief Discriminator identifying the concrete type stored in a GHDFObjectValue or array element.
 *
 * Values from GHDFValueType_UInt8 through GHDFValueType_EncodedInteger are the valid "container"
 * types accepted as entry/element types; GHDFValueType_None is the unset/invalid sentinel.
 */
typedef enum GHDFValueTypeEnum
{
    /** Unset/invalid type sentinel; not a valid entry or array element type. */
    GHDFValueType_None = 0,
    GHDFValueType_UInt8 = 1,
    GHDFValueType_Int8 = 2,
    GHDFValueType_Int16 = 3,
    GHDFValueType_UInt16 = 4,
    GHDFValueType_Int32 = 5,
    GHDFValueType_UInt32 = 6,
    /** Fixed-width 64-bit signed integer (stored/serialized as 8 bytes). */
    GHDFValueType_Int64 = 7,
    GHDFValueType_UInt64 = 8,
    GHDFValueType_Float = 9,
    GHDFValueType_Double = 10,
    GHDFValueType_Boolean = 11,
    /** UTF-8 string held in a pool-borrowed GenericBuffer (see GHDFObjectValue.Value.String). */
    GHDFValueType_String = 12,
    /** Nested keyed container; the value holds a pool-borrowed GHDFCompound*. */
    GHDFValueType_Compound = 13,
    /**
     * Signed 64-bit integer that is variable-length (LEB-style) encoded on the wire rather than
     * stored as a fixed 8 bytes; in memory it is a plain int64_t. Use for integers whose magnitude
     * is usually small to save space versus GHDFValueType_Int64.
     */
    GHDFValueType_EncodedInteger = 14
} GHDFValueType;

/**
 * @brief Full type descriptor for a compound entry: the element type plus whether it is an array.
 *
 * When IsArray is false the entry holds a single value of ValueType. When IsArray is true the entry
 * holds a GHDFArray whose elements are of ValueType. Build these with GHDF_CreateRegularType /
 * GHDF_CreateArrayType, or by aggregate initialization.
 */
typedef struct GHDFCompoundEntryTypeStruct
{
    /** Concrete value type of the entry (or of each array element); must be a valid container type. */
    GHDFValueType ValueType;
    /** true if the entry is a GHDFArray of ValueType; false if it is a single ValueType value. */
    bool IsArray;
} GHDFCompoundEntryType;

/**
 * @brief A tagged union holding one typed GHDF value (scalar, string, compound, or array).
 *
 * Type selects which union member is active. For String/Compound/Array the union holds a borrowed
 * pointer the value does not own (ownership stays with the originating GHDFObjectPool). Construct
 * instances with the GHDFObjectValue_Create* helpers rather than setting fields by hand.
 *
 * @note For an array value, Type is set to the array's ELEMENT type (e.g. GHDFValueType_Int32),
 *       not a distinct "array" tag — see GHDFObjectValue_CreateArray. The "this is an array"
 *       distinction lives in the accompanying GHDFCompoundEntryType.IsArray, not here.
 */
typedef struct GHDFObjectValueStruct
{
    /** Active union member selector / value type. For arrays this is the element type. */
    GHDFValueType Type;
    union
    {
        uint8_t UInt8;
        int8_t Int8;
        uint16_t UInt16;
        int16_t Int16;
        uint32_t UInt32;
        int32_t Int32;
        uint64_t UInt64;
        int64_t Int64;
        float Float;
        double Double;
        bool Boolean;
        /** Borrowed UTF-8 string buffer (with trailing NUL); not owned by this value. */
        GenericBuffer* String;
        /** Borrowed nested compound; not owned by this value. */
        GHDFCompound* Compound;
        /** Borrowed array; not owned by this value. Active for array-typed entries. */
        GHDFArray* Array;
        /** Variable-length-encoded signed 64-bit integer value (plain int64_t in memory). */
        int64_t EncodedInteger;
    } Value;
} GHDFObjectValue;

/**
 * @brief One key/type/value triple as enumerated from a compound's entry collection.
 *
 * Produced when iterating GHDFCompound_GetEntryCollection: pairs the entry's ID with its full
 * type descriptor and current value.
 */
typedef struct GHDFCompoundEntryStruct
{
    /** The entry's key within its compound. */
    GHDFEntryID Id;
    /** The entry's declared type (value type + array flag). */
    GHDFCompoundEntryType EntryType;
    /** The entry's current value (members borrowed, as for any GHDFObjectValue). */
    GHDFObjectValue Value;
} GHDFCompoundEntry;

/**
 * @brief An array element paired with its position, as enumerated from an array's element collection.
 *
 * Produced when iterating GHDFArray_GetElementCollection.
 */
typedef struct GHDFArrayIndexedValueStruct
{
    /** Zero-based position of the element within the array. */
    size_t Index;
    /** The element's value, typed as the array's element type. */
    GHDFObjectValue Value;
} GHDFArrayIndexedValue;


// Functions.

/**
 * @brief Build a GHDFObjectValue holding an unsigned 8-bit integer.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_UInt8 and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateUInt8(uint8_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_UInt8, .Value.UInt8 = value };
}

/**
 * @brief Build a GHDFObjectValue holding a signed 8-bit integer.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_Int8 and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateInt8(int8_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Int8, .Value.Int8 = value };
}

/**
 * @brief Build a GHDFObjectValue holding a signed 16-bit integer.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_Int16 and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateInt16(int16_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Int16, .Value.Int16 = value };
}

/**
 * @brief Build a GHDFObjectValue holding an unsigned 16-bit integer.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_UInt16 and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateUInt16(uint16_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_UInt16, .Value.UInt16 = value };
}

/**
 * @brief Build a GHDFObjectValue holding a signed 32-bit integer.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_Int32 and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateInt32(int32_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Int32, .Value.Int32 = value };
}

/**
 * @brief Build a GHDFObjectValue holding an unsigned 32-bit integer.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_UInt32 and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateUInt32(uint32_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_UInt32, .Value.UInt32 = value };
}

/**
 * @brief Build a GHDFObjectValue holding a fixed-width signed 64-bit integer.
 *
 * For an integer that should be variable-length encoded on the wire, use
 * GHDFObjectValue_CreateEncodedInteger instead.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_Int64 and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateInt64(int64_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Int64, .Value.Int64 = value };
}

/**
 * @brief Build a GHDFObjectValue holding an unsigned 64-bit integer.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_UInt64 and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateUInt64(uint64_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_UInt64, .Value.UInt64 = value };
}

/**
 * @brief Build a GHDFObjectValue holding a single-precision float.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_Float and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateFloat(float value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Float, .Value.Float = value };
}

/**
 * @brief Build a GHDFObjectValue holding a double-precision float.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_Double and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateDouble(double value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Double, .Value.Double = value };
}

/**
 * @brief Build a GHDFObjectValue holding a boolean.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_Boolean and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateBoolean(bool value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Boolean, .Value.Boolean = value };
}

/**
 * @brief Build a GHDFObjectValue referencing a UTF-8 string buffer.
 *
 * The buffer is stored by reference (borrowed), not copied; this helper performs no validation.
 * When the value is later handed to a compound/array, the buffer must be a pool-borrowed UTF-8
 * GenericBuffer (element size 1) ending in a NUL terminator, or the storing call is rejected.
 * @param value The borrowed string buffer to reference. Ownership is NOT transferred to the value;
 *        the buffer must outlive every use of the returned value and remain owned by its pool.
 * @returns A GHDFObjectValue with Type GHDFValueType_String referencing the buffer.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateString(GenericBuffer* value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_String, .Value.String = value };
}

/**
 * @brief Build a GHDFObjectValue referencing a nested compound.
 *
 * The compound is stored by reference (borrowed), not copied. When stored into a parent
 * compound/array it must be a pool-borrowed GHDFCompound or the storing call is rejected.
 * @param value The borrowed compound to reference. Ownership is NOT transferred; it must remain
 *        owned by its pool and outlive every use of the returned value.
 * @returns A GHDFObjectValue with Type GHDFValueType_Compound referencing the compound.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateCompound(GHDFCompound* value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_Compound, .Value.Compound = value };
}

/**
 * @brief Build a GHDFObjectValue referencing an array, tagged with the array's element type.
 *
 * Note the Type field is set to valueType (the array's ELEMENT type), not a dedicated array tag;
 * the array-ness of an entry is carried separately by GHDFCompoundEntryType.IsArray. valueType must
 * match the array's actual element type when the value is stored, or the storing call is rejected.
 * The array is borrowed, not copied.
 * @param value The borrowed array to reference. Ownership is NOT transferred; it must remain owned
 *        by its pool and outlive every use of the returned value.
 * @param valueType The element type of the array; written into the value's Type field.
 * @returns A GHDFObjectValue with Type set to valueType referencing the array.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateArray(GHDFArray* value, GHDFValueType valueType)
{
    return (GHDFObjectValue){ .Type = valueType, .Value.Array = value };
}

/**
 * @brief Build a GHDFObjectValue holding a variable-length-encoded signed 64-bit integer.
 *
 * In memory the value is a plain int64_t; the "encoded" type only affects the on-wire form
 * (compact LEB-style encoding) versus GHDFValueType_Int64's fixed 8 bytes.
 * @param value The value to store.
 * @returns A GHDFObjectValue with Type GHDFValueType_EncodedInteger and the given value.
 */
static inline GHDFObjectValue GHDFObjectValue_CreateEncodedInteger(int64_t value)
{
    return (GHDFObjectValue){ .Type = GHDFValueType_EncodedInteger, .Value.EncodedInteger = value };
}

/**
 * @brief Serialize a compound tree to a stream as a complete GHDF document.
 *
 * Writes the GHDF signature, format version, then the root compound's body (recursively including
 * all nested compounds, arrays, and strings) to the stream in little-endian order. The stream is
 * advanced by the bytes written; the stream itself is not closed by this call. root is read only
 * and is not modified.
 * @param root The compound to serialize as the document root. Must not be NULL.
 * @param stream Destination byte stream, positioned where the document should begin. Must not be NULL.
 * @returns Success once the whole document has been written. Raises ErrorCode_IllegalArgument if
 *          root or stream is NULL. If finalizing the underlying binary stream fails, that cleanup
 *          error is returned in preference to any write error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDF_Write(const GHDFCompound* root, IOStream* stream);

/**
 * @brief Read a complete GHDF document from a stream into a freshly borrowed compound tree.
 *
 * Validates the signature and version, then reconstructs the root compound and all nested
 * structures, borrowing every compound, array, and string from objectPool. On success *outRoot
 * receives the root compound, which (together with everything nested under it) is OWNED BY THE
 * CALLER's responsibility to return to objectPool — return the root via
 * GHDFObjectPool_ReturnCompound(objectPool, root, true), or simply deconstruct the pool to reclaim
 * everything. The stream is read from its current position and is not closed.
 *
 * On any failure *outRoot is set to NULL and any partially built tree is returned to the pool, so
 * the caller never owns anything after an error.
 * @param stream Source byte stream positioned at the document start. Must not be NULL.
 * @param objectPool Pool from which all reconstructed objects are borrowed. Must not be NULL.
 * @param outRoot [out] Receives the borrowed root compound on success, or NULL on failure. Must not
 *        be NULL.
 * @returns Success with *outRoot set. Raises ErrorCode_IllegalArgument if any argument is NULL;
 *          ErrorCode_Deserialize if the signature is wrong, the version is unsupported, a type byte
 *          is invalid, or a declared length exceeds the platform range. If finalizing the underlying
 *          binary stream fails, that cleanup error is returned in preference to any read error.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDF_Read(IOStream* stream, GHDFObjectPool* objectPool, GHDFCompound** outRoot);

/**
 * @brief Remove all entries from a compound, releasing each entry's borrowed sub-resources.
 *
 * For every entry, any nested compound, array, or string it holds is returned to its owning pool
 * before the entry is dropped, leaving the compound empty (entry count 0) and reusable. The
 * compound object itself is not returned to its pool.
 * @param self The compound to clear. Must not be NULL.
 * @returns Success when the compound is empty. Raises ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidState if a stored entry is found to be corrupt during the sweep.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFCompound_Clear(GHDFCompound* self);

/**
 * @brief Remove a single entry by ID, releasing the borrowed sub-resources it holds.
 *
 * If the entry holds a nested compound, array, or string, that resource is returned to its owning
 * pool before the entry is removed.
 * @param self The compound to modify. Must not be NULL.
 * @param id Key of the entry to remove. Must not be GHDF_ENTRY_ID_INVALID.
 * @returns Success once the entry is removed. Raises ErrorCode_IllegalArgument if self is NULL or id
 *          is GHDF_ENTRY_ID_INVALID; ErrorCode_InvalidOperation if no entry with that ID exists.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFCompound_Remove(GHDFCompound* self, GHDFEntryID id);

/**
 * @brief Insert or overwrite the entry at the given ID with a typed value.
 *
 * The value is validated against entryType: its Type must match entryType.ValueType, and any
 * referenced string/compound/array must be a properly pool-borrowed resource (an array's element
 * type must also match). The GHDFObjectValue is stored by value, so its referenced sub-resource is
 * adopted into this compound; the compound takes over responsibility for returning that sub-resource
 * to its pool when the entry is later cleared/removed/overwritten. If an entry already existed at id,
 * its previous sub-resource is released after the new value is installed. The caller must not also
 * return a sub-resource it has handed to this compound.
 * @param self The compound to modify. Must not be NULL.
 * @param id Key to assign. Must not be GHDF_ENTRY_ID_INVALID.
 * @param entryType The declared type the value must conform to (value type + array flag).
 * @param value The value to store; read only. Must not be NULL and must match entryType.
 * @returns Success once stored. Raises ErrorCode_IllegalArgument if self or value is NULL, id is
 *          invalid, entryType's value type is not a valid container type, or a referenced
 *          string/compound/array is not pool-borrowed; ErrorCode_InvalidOperation if value->Type
 *          (or an array's element type) does not match entryType.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFCompound_SetValue(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType entryType,
    const GHDFObjectValue* value);

/**
 * @brief Look up an entry by ID, requiring it to exist.
 *
 * Copies the stored value into *outEntry. The returned value's String/Compound/Array members (if
 * any) alias the resources still owned by the compound; the caller must not return or free them.
 * @param self The compound to query. Must not be NULL.
 * @param id Key to look up.
 * @param outEntry [out] Receives a copy of the stored value on success. Must not be NULL.
 * @returns Success if found. Raises ErrorCode_IllegalArgument if self or outEntry is NULL;
 *          ErrorCode_InvalidOperation if no entry with that ID exists.
 */
Error GHDFCompound_Get(GHDFCompound* self, GHDFEntryID id, GHDFObjectValue* outEntry);

/**
 * @brief Look up an entry by ID, treating absence as a non-error.
 *
 * If found, sets *outWasFound to true and copies the value into *outEntry; if not found, sets
 * *outWasFound to false and leaves *outEntry unchanged. Returned sub-resources are aliases owned by
 * the compound and must not be returned/freed by the caller.
 * @param self The compound to query. Must not be NULL.
 * @param id Key to look up.
 * @param outEntry [out] Receives a copy of the stored value when found. Must not be NULL.
 * @param outWasFound [out] Set to true if the entry exists, false otherwise. Must not be NULL.
 * @returns Success whether or not the entry exists. Raises ErrorCode_IllegalArgument if self,
 *          outEntry, or outWasFound is NULL.
 */
Error GHDFCompound_GetOptional(GHDFCompound* self, GHDFEntryID id, GHDFObjectValue* outEntry, bool* outWasFound);

/**
 * @brief Look up an entry by ID and assert it has the expected type.
 *
 * Like GHDFCompound_Get but additionally fails unless the stored entry's value type and array flag
 * both equal expectedType. Returned sub-resources are aliases owned by the compound.
 * @param self The compound to query. Must not be NULL.
 * @param id Key to look up.
 * @param expectedType The type (value type + array flag) the entry must have; its value type must be
 *        a valid container type.
 * @param outEntry [out] Receives a copy of the stored value on success. Must not be NULL.
 * @returns Success if found and the type matches. Raises ErrorCode_IllegalArgument if self or
 *          outEntry is NULL or expectedType's value type is invalid; ErrorCode_InvalidOperation if
 *          the entry is missing or its type differs from expectedType.
 */
Error GHDFCompound_GetVerified(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType expectedType,
    GHDFObjectValue* outEntry);

/**
 * @brief Look up an entry by ID, treating absence as a non-error but type mismatch as an error.
 *
 * If the entry is absent, sets *outWasFound to false and returns success. If present, sets
 * *outWasFound to true, copies the value into *outEntry, and then verifies it against expectedType
 * exactly as GHDFCompound_GetVerified does. Returned sub-resources are aliases owned by the compound.
 * @param self The compound to query. Must not be NULL.
 * @param id Key to look up.
 * @param expectedType The type the entry must have if present.
 * @param outEntry [out] Receives a copy of the stored value when found. Must not be NULL.
 * @param outWasFound [out] Set to true if the entry exists, false otherwise. Must not be NULL.
 * @returns Success if the entry is absent, or present with the expected type. Raises
 *          ErrorCode_IllegalArgument if self, outEntry, or outWasFound is NULL;
 *          ErrorCode_InvalidOperation if the entry is present but its type differs from expectedType.
 */
Error GHDFCompound_GetOptionalVerified(GHDFCompound* self,
    GHDFEntryID id,
    GHDFCompoundEntryType expectedType,
    GHDFObjectValue* outEntry,
    bool* outWasFound);

/**
 * @brief Return the number of entries currently in the compound.
 * @param self The compound to query. May be NULL.
 * @returns The entry count, or 0 if self is NULL.
 */
size_t GHDFCompound_GetEntryCount(GHDFCompound* self);

/**
 * @brief Get a read-only collection view that enumerates the compound's entries.
 *
 * Iterating the returned collection yields GHDFCompoundEntry values (ID + type + value). The
 * collection is owned by the compound and stays valid as long as the compound is alive and
 * unmodified; do not free it. Mutating the compound during enumeration invalidates the enumerator.
 * @param self The compound to view. May be NULL.
 * @returns The entry collection, or NULL if self is NULL.
 */
ICollection* GHDFCompound_GetEntryCollection(GHDFCompound* self);

/**
 * @brief Get a read-only collection view that enumerates the compound's values.
 *
 * Iterating the returned collection yields GHDFObjectValue values (without their keys). Owned by the
 * compound; valid while the compound is alive and unmodified; do not free it.
 * @param self The compound to view. May be NULL.
 * @returns The value collection, or NULL if self is NULL.
 */
ICollection* GHDFCompound_GetValueCollection(GHDFCompound* self);

/**
 * @brief Get a read-only collection view that enumerates the compound's keys.
 *
 * Iterating the returned collection yields GHDFEntryID values. Owned by the compound; valid while
 * the compound is alive and unmodified; do not free it.
 * @param self The compound to view. May be NULL.
 * @returns The key collection, or NULL if self is NULL.
 */
ICollection* GHDFCompound_GetKeyCollection(GHDFCompound* self);

/**
 * @brief Remove all elements from the array, releasing any borrowed sub-resources they hold.
 *
 * For element types that reference resources (string/compound), each element's resource is returned
 * to its owning pool before the array is emptied. The array's element type is preserved, so it can
 * be reused for more elements of the same type.
 * @param self The array to clear. Must not be NULL.
 * @returns Success when the array is empty. Raises ErrorCode_IllegalArgument if self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFArray_Clear(GHDFArray* self);

/**
 * @brief Remove the element at the given index, releasing any borrowed sub-resource it holds.
 *
 * Elements after index shift down by one. If the removed element references a string/compound, that
 * resource is returned to its owning pool.
 * @param self The array to modify. Must not be NULL.
 * @param index Zero-based index of the element to remove.
 * @returns Success once removed. Raises ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_ArgumentOutOfRange if index is not less than the element count.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFArray_RemoveAt(GHDFArray* self, size_t index);

/**
 * @brief Append a value to the end of the array.
 *
 * Convenience wrapper for inserting at the current element count; see GHDFArray_InsertValue for the
 * type/ownership contract.
 * @param self The array to append to. Must not be NULL.
 * @param value The value to append; read only. Must not be NULL and must match the array's element
 *        type. Any referenced sub-resource is adopted by the array (do not also return it yourself).
 * @returns Success once appended. See GHDFArray_InsertValue for the error conditions.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFArray_AddValue(GHDFArray* self, const GHDFObjectValue* value);

/**
 * @brief Insert a value at the given index, shifting later elements up by one.
 *
 * The value's Type must equal the array's element type (and, for string/compound elements, it must
 * reference a properly pool-borrowed resource). Scalars are copied into the array's packed storage;
 * a referenced string/compound is adopted by the array, which becomes responsible for returning it
 * to its pool when the element is later removed/replaced/cleared. The caller must not also return an
 * adopted sub-resource. Arrays cannot contain array-typed elements.
 * @param self The array to modify. Must not be NULL.
 * @param index Zero-based insertion position; valid range is [0, element count].
 * @param value The value to insert; read only. Must not be NULL and must match the element type.
 * @returns Success once inserted. Raises ErrorCode_IllegalArgument if self or value is NULL, the
 *          array has no element type set, the value would be an array element, or a referenced
 *          string/compound is not pool-borrowed; ErrorCode_InvalidOperation on a value/element type
 *          mismatch; ErrorCode_ArgumentOutOfRange if index exceeds the element count.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFArray_InsertValue(GHDFArray* self, size_t index, const GHDFObjectValue* value);

/**
 * @brief Overwrite the element at the given index with a new value.
 *
 * The new value must match the array's element type under the same rules as GHDFArray_InsertValue.
 * The new sub-resource (if any) is adopted by the array and the previously stored element's
 * sub-resource is released back to its pool after the replacement succeeds.
 * @param self The array to modify. Must not be NULL.
 * @param index Zero-based index of the element to overwrite.
 * @param value The replacement value; read only. Must not be NULL and must match the element type.
 * @returns Success once replaced. Raises ErrorCode_IllegalArgument if self or value is NULL, the
 *          value would be an array element, or a referenced string/compound is not pool-borrowed;
 *          ErrorCode_InvalidOperation on a type mismatch; ErrorCode_ArgumentOutOfRange if index is
 *          not less than the element count.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFArray_ReplaceValue(GHDFArray* self, size_t index, const GHDFObjectValue* value);

/**
 * @brief Read the element at the given index into a GHDFObjectValue.
 *
 * The out value is tagged with the array's element type. For string/compound element types the
 * returned String/Compound member aliases the resource still owned by the array (do not return or
 * free it).
 * @param self The array to query. Must not be NULL.
 * @param index Zero-based index of the element to read.
 * @param outValue [out] Receives the element value. Must not be NULL.
 * @returns Success if read. Raises ErrorCode_IllegalArgument if self or outValue is NULL;
 *          ErrorCode_ArgumentOutOfRange if index is not less than the element count.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFArray_Get(GHDFArray* self, size_t index, GHDFObjectValue* outValue);

/**
 * @brief Bulk-append a contiguous run of array elements to a buffer without per-element conversion.
 *
 * Copies elementCount elements starting at startIndex straight from the array's packed in-memory
 * storage onto the end of destination. The destination must use the same element layout as this GHDF
 * array's storage (e.g. a UInt8 array appends bytes, an Int16 array appends int16_t values), i.e. its
 * element size must equal the array's storage element size. Only fixed-width element types support
 * this fast path; String and Compound arrays (and an array with no element type) do not. Copying zero
 * elements is a no-op success.
 * @param self The source array. Must not be NULL.
 * @param startIndex Zero-based index of the first element to copy; valid range is [0, element count].
 * @param elementCount Number of elements to copy; startIndex + elementCount must not exceed the
 *        element count.
 * @param destination Buffer to append to; its element size must match the array's storage element
 *        size. Must not be NULL.
 * @returns Success once the elements are appended. Raises ErrorCode_IllegalArgument if self or
 *          destination is NULL, the element type is None, the element type does not support raw copy,
 *          or destination's element size differs from the storage element size;
 *          ErrorCode_ArgumentOutOfRange if startIndex or the requested range is out of bounds;
 *          ErrorCode_BufferTooLarge if the byte size would overflow the address space;
 *          ErrorCode_BufferTooSmall if appending to destination fails.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFArray_CopyRawBytes(GHDFArray* self,
    size_t startIndex,
    size_t elementCount,
    GenericBuffer* destination);

/**
 * @brief Return the number of elements currently in the array.
 * @param self The array to query. May be NULL.
 * @returns The element count, or 0 if self is NULL.
 */
size_t GHDFArray_GetElementCount(GHDFArray* self);

/**
 * @brief Return the element type shared by all of the array's elements.
 * @param self The array to query. May be NULL.
 * @returns The element's GHDFValueType, or GHDFValueType_None if self is NULL.
 */
GHDFValueType GHDFArray_GetElementType(GHDFArray* self);

/**
 * @brief Get a read-only collection view that enumerates the array's elements with their indices.
 *
 * Iterating the returned collection yields GHDFArrayIndexedValue values (index + value). The
 * collection is owned by the array and stays valid while the array is alive and unmodified; do not
 * free it. Mutating the array during enumeration invalidates the enumerator.
 * @param self The array to view. May be NULL.
 * @returns The element collection, or NULL if self is NULL.
 */
ICollection* GHDFArray_GetElementCollection(GHDFArray* self);


/**
 * @brief Allocate and initialize a new GHDF object pool.
 *
 * On success *outPool receives a ready-to-use pool that the caller owns and must later destroy with
 * GHDFObjectPool_Deconstruct.
 * @param outPool [out] Receives the new pool on success, or NULL on failure. Must not be NULL.
 * @returns Success with *outPool set. Raises ErrorCode_IllegalArgument if outPool is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFObjectPool_Create(GHDFObjectPool** outPool);

/**
 * @brief Destroy a pool and free every object it owns (including objects still borrowed out).
 *
 * Tears down the compound, array, and string sub-pools and frees the pool itself. After this call any
 * compound, array, or string previously borrowed from the pool is invalid and must not be used or
 * returned. Passing NULL is treated as an error rather than a silent no-op.
 * @param self The pool to destroy. Must not be NULL.
 * @returns Success once the pool is freed. Raises ErrorCode_IllegalArgument if self is NULL. If a
 *          sub-pool teardown fails the first such error is returned (the pool memory is still freed).
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFObjectPool_Deconstruct(GHDFObjectPool* self);

/**
 * @brief Borrow an empty compound from the pool.
 *
 * The returned compound is reset (no entries) and tagged as owned by this pool. Return it later with
 * GHDFObjectPool_ReturnCompound (or let GHDFObjectPool_Deconstruct reclaim it).
 * @param self The pool to borrow from. Must not be NULL.
 * @param outCompound [out] Receives the borrowed compound on success, or NULL on failure. Must not be
 *        NULL.
 * @returns Success with *outCompound set. Raises ErrorCode_IllegalArgument if self or outCompound is
 *          NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFObjectPool_BorrowCompound(GHDFObjectPool* self, GHDFCompound** outCompound);

/**
 * @brief Borrow an empty array configured for a specific element type.
 *
 * The returned array has no elements, is set to hold elements of elementType, and is tagged as owned
 * by this pool. Return it later with GHDFObjectPool_ReturnArray.
 * @param self The pool to borrow from. Must not be NULL.
 * @param elementType The element type the array will hold; must be a valid container type (not None
 *        and not an array).
 * @param outArray [out] Receives the borrowed array on success, or NULL on failure. Must not be NULL.
 * @returns Success with *outArray set. Raises ErrorCode_IllegalArgument if self or outArray is NULL,
 *          or elementType is not a valid element type.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFObjectPool_BorrowArray(GHDFObjectPool* self, GHDFValueType elementType, GHDFArray** outArray);

/**
 * @brief Borrow an empty UTF-8 string buffer from the pool.
 *
 * The returned GenericBuffer is cleared, has element size 1, and is tagged as owned by this pool. Fill
 * it with UTF-8 bytes (and ensure a trailing NUL) before storing it as a String value. Return it
 * later with GHDFObjectPool_ReturnString.
 * @param self The pool to borrow from. Must not be NULL.
 * @param outStringBuffer [out] Receives the borrowed buffer on success, or NULL on failure. Must not
 *        be NULL.
 * @returns Success with *outStringBuffer set. Raises ErrorCode_IllegalArgument if self or
 *          outStringBuffer is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFObjectPool_BorrowString(GHDFObjectPool* self, GenericBuffer** outStringBuffer);

/**
 * @brief Return a previously borrowed compound to the pool for reuse.
 *
 * The compound must have been borrowed from this same pool. It is reset and made available again;
 * after returning it the caller must not use the pointer. Returning NULL is a no-op success. Any
 * sub-resources held by the compound's entries are released as part of resetting it.
 * @param self The pool that owns the compound. Must not be NULL.
 * @param compound The compound to return. May be NULL (no-op). Must belong to self if non-NULL.
 * @param includeNestedStructures Accepted for API symmetry; nested string/array/compound resources
 *        are always released regardless of this flag.
 * @returns Success once returned. Raises ErrorCode_IllegalArgument if self is NULL, or compound is
 *          non-NULL but was not borrowed from this pool.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFObjectPool_ReturnCompound(GHDFObjectPool* self, GHDFCompound* compound, bool includeNestedStructures);

/**
 * @brief Return a previously borrowed array to the pool for reuse.
 *
 * The array must have been borrowed from this same pool. It is reset and made available again; after
 * returning it the caller must not use the pointer. Returning NULL is a no-op success. Any
 * sub-resources held by the array's elements are released as part of resetting it.
 * @param self The pool that owns the array. Must not be NULL.
 * @param array The array to return. May be NULL (no-op). Must belong to self if non-NULL.
 * @param includeNestedStructures Accepted for API symmetry; nested resources are always released
 *        regardless of this flag.
 * @returns Success once returned. Raises ErrorCode_IllegalArgument if self is NULL, or array is
 *          non-NULL but was not borrowed from this pool.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFObjectPool_ReturnArray(GHDFObjectPool* self, GHDFArray* array, bool includeNestedStructures);

/**
 * @brief Return a previously borrowed string buffer to the pool for reuse.
 *
 * The buffer must have been borrowed from this same pool. It is cleared and made available again;
 * after returning it the caller must not use the pointer. Returning NULL is a no-op success.
 * @param self The pool that owns the buffer. Must not be NULL.
 * @param stringBuffer The string buffer to return. May be NULL (no-op). Must belong to self if
 *        non-NULL.
 * @returns Success once returned. Raises ErrorCode_IllegalArgument if self is NULL, or stringBuffer
 *          is non-NULL but was not borrowed from this pool.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error GHDFObjectPool_ReturnString(GHDFObjectPool* self, GenericBuffer* stringBuffer);

/**
 * @brief Build a non-array (scalar/string/compound) compound entry type descriptor.
 * @param valueType The value type of the entry.
 * @returns A GHDFCompoundEntryType with the given value type and IsArray set to false.
 */
GHDFCompoundEntryType GHDF_CreateRegularType(GHDFValueType valueType);

/**
 * @brief Build an array compound entry type descriptor for a given element type.
 * @param valueType The element type of the array entry.
 * @returns A GHDFCompoundEntryType with the given value type and IsArray set to true.
 */
GHDFCompoundEntryType GHDF_CreateArrayType(GHDFValueType valueType);
