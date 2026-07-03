#pragma once
#include "WRError.h"
#include <stdint.h>
#include "WRMemory.h"
#include "WRError.h"
#include "WRCollection.h"


/**
 * This module provides JSON serialization and deserialization stuff.
 * 
 * All JSON strings, compounds and arrays must be acquired from a JSON object pool, 
 * then returned to the pool when no longer used.
 * 
 * Compound string keys are copied into pool-managed storage when inserted.
 * 
 * String values are stored as generic buffers borrowed from the pool.
 * 
 * The compound and array get functions work with the following mannet:
 * - The regular get functions return an error if no value is found, otherwise return the value.
 * - Optional modifier doesnt return an error for no value found, it also returns whether the value was found.
 * - The verified modifier, if a value is present, additionally returns an error if the value is not of the expected type.
 * Both optional and verified modifiers can be present at the same time.
 */



// Types.
/**
 * @brief Opaque JSON object (a `{...}` map of string keys to JSON values).
 *
 * A compound holds an unordered set of key/value entries. Keys are UTF-8 byte strings and values are
 * JSONObjectValue. A compound is always owned by exactly one JSONObjectPool: it must be obtained from
 * a pool via JSONObjectPool_BorrowCompound and given back with JSONObjectPool_ReturnCompound (or
 * indirectly via JSONObjectPool_ReturnValue / a parent container's release). Key bytes are copied into
 * pool-managed storage on insertion; the caller's key buffer is not retained.
 */
typedef struct JSONCompoundStruct JSONCompound;
/**
 * @brief Opaque JSON array (an ordered `[...]` sequence of JSON values).
 *
 * Elements are JSONObjectValue stored in insertion/index order. An array is always owned by exactly one
 * JSONObjectPool: obtain it with JSONObjectPool_BorrowArray and release it with JSONObjectPool_ReturnArray
 * (or indirectly via JSONObjectPool_ReturnValue / a parent container's release).
 */
typedef struct JSONArrayStruct JSONArray;
/**
 * @brief Opaque allocator/owner for all JSON compounds, arrays and string buffers in a document.
 *
 * Every JSONCompound, JSONArray and string GenericBuffer used with this module must be borrowed from a
 * pool and later returned to the same pool; values returned by the parser are owned by the pool passed to
 * JSON_Deserialize. The pool also owns the duplicated storage backing every compound key. Create with
 * JSONObjectPool_Create and destroy with JSONObjectPool_Deconstruct, which frees every still-borrowed
 * object and all key storage.
 */
typedef struct JSONObjectPoolStruct JSONObjectPool;

/**
 * @brief Discriminator identifying which kind of value a JSONObjectValue currently holds.
 *
 * Selects the active member of JSONObjectValue::Value and is also the type passed to the `*GetVerified`
 * accessors to assert an expected value kind.
 */
typedef enum JSONValueTypeEnum
{
    JSONValueType_None = 0,     /**< No value present (empty/cleared slot); not a serializable JSON type. */
    JSONValueType_String = 1,   /**< UTF-8 string, held as a pool-owned GenericBuffer of bytes. */
    JSONValueType_Boolean = 2,  /**< JSON `true`/`false`. */
    JSONValueType_Integer = 3,  /**< Integral JSON number stored as int64_t. */
    JSONValueType_RealNumber = 4, /**< Fractional/exponent JSON number stored as double; must be finite to serialize. */
    JSONValueType_Null = 5,     /**< JSON `null`. */
    JSONValueType_Compound = 6, /**< Nested JSON object (JSONCompound). */
    JSONValueType_Array = 7,    /**< Nested JSON array (JSONArray). */
} JSONValueType; 

/**
 * @brief A tagged union holding a single JSON value of any kind.
 *
 * `Type` selects which `Value` member is valid. This struct is a small by-value handle: copying it copies
 * only the tag and (for String/Compound/Array) a borrowed pointer to pool-owned storage, never the
 * underlying data. The pointed-to String/Compound/Array remains owned by its JSONObjectPool; storing a
 * value into a container or returning it via JSONObjectPool_ReturnValue transfers that ownership/lifetime
 * responsibility per the relevant function's contract. Construct instances with the JSONObjectValue_Create*
 * helpers.
 */
typedef struct JSONObjectValueStruct
{
    JSONValueType Type; /**< Active-member discriminator; determines which `Value` field is meaningful. */
    union 
    {
        int64_t Integer;        /**< Valid when Type == JSONValueType_Integer. */
        double RealNumber;      /**< Valid when Type == JSONValueType_RealNumber. */
        bool Boolean;           /**< Valid when Type == JSONValueType_Boolean. */
        GenericBuffer* String;  /**< Valid when Type == JSONValueType_String; borrowed, pool-owned UTF-8 byte buffer. */
        JSONCompound* Compound; /**< Valid when Type == JSONValueType_Compound; borrowed, pool-owned object. */
        JSONArray* Array;       /**< Valid when Type == JSONValueType_Array; borrowed, pool-owned array. */
        void* Null;             /**< Valid when Type == JSONValueType_Null; always NULL, carries no data. */
    } Value;
} JSONObjectValue;

/**
 * @brief Bit flags controlling JSON_Serialize output formatting. Combine with bitwise OR.
 */
typedef enum JSONSerializeFlagsEnum
{
    JSONSerializeFlags_None = 0,                    /**< Compact output, no trailing null byte. */
    JSONSerializeFlags_NullTerminated = (1 << 0),   /**< Append a terminating null byte after the JSON text. */
    JSONSerializeFlags_PrettyPrinted = (1 << 1),    /**< Indented, multi-line "pretty" layout instead of compact. */
} JSONSerializeFlags;

/**
 * @brief A single key/value pair surfaced while iterating a compound's entry collection.
 *
 * This is the element type produced by the collection returned from JSONCompound_GetEntryCollection. It is
 * a read-only snapshot: both fields borrow into pool-owned storage and are valid only until the compound is
 * structurally modified or its backing objects are returned to the pool.
 */
typedef struct JSONCompoundEntryStruct
{
    const unsigned char* Key;   /**< Borrowed, NUL-terminated UTF-8 key owned by the pool; do not free or mutate. */
    JSONObjectValue Value;      /**< Copy of the stored value (any String/Compound/Array pointer remains pool-owned). */
} JSONCompoundEntry;

/**
 * @brief A single index/value pair surfaced while iterating an array's element collection.
 *
 * Element type produced by the collection returned from JSONArray_GetElementCollection. A read-only
 * snapshot whose Value borrows into pool-owned storage; valid only until the array is modified or its
 * backing objects are returned to the pool.
 */
typedef struct JSONArrayIndexedValueStruct
{
    size_t Index;               /**< Zero-based position of this element within the array. */
    JSONObjectValue Value;      /**< Copy of the element value (any String/Compound/Array pointer remains pool-owned). */
} JSONArrayIndexedValue;


// Functions.
/**
 * @brief Wrap a string buffer into a JSONObjectValue tagged JSONValueType_String.
 *
 * Builds the tagged union only; the buffer is not copied or validated. The caller retains ownership: the
 * buffer must be one borrowed from the pool that will own the containing structure, and it stays alive
 * independently until placed in a container or returned to its pool.
 * @param value Borrowed, pool-owned UTF-8 byte buffer to reference. May be NULL, but a NULL string cannot
 *        be serialized and will be rejected by JSON_Serialize.
 * @returns A JSONObjectValue of type JSONValueType_String referencing @p value.
 */
static inline JSONObjectValue JSONObjectValue_CreateString(GenericBuffer* value)
{
    return (JSONObjectValue){ .Type = JSONValueType_String, .Value.String = value };
}

/**
 * @brief Wrap a boolean into a JSONObjectValue tagged JSONValueType_Boolean.
 * @param value The boolean to store.
 * @returns A JSONObjectValue of type JSONValueType_Boolean holding @p value.
 */
static inline JSONObjectValue JSONObjectValue_CreateBoolean(bool value)
{
    return (JSONObjectValue){ .Type = JSONValueType_Boolean, .Value.Boolean = value };
}

/**
 * @brief Wrap a 64-bit signed integer into a JSONObjectValue tagged JSONValueType_Integer.
 * @param value The integer to store.
 * @returns A JSONObjectValue of type JSONValueType_Integer holding @p value.
 */
static inline JSONObjectValue JSONObjectValue_CreateInteger(int64_t value)
{
    return (JSONObjectValue){ .Type = JSONValueType_Integer, .Value.Integer = value };
}

/**
 * @brief Wrap a double into a JSONObjectValue tagged JSONValueType_RealNumber.
 * @param value The real number to store. Non-finite values (NaN/Inf) are accepted here but are not valid
 *        JSON and cause JSON_Serialize to fail with ErrorCode_Serialize.
 * @returns A JSONObjectValue of type JSONValueType_RealNumber holding @p value.
 */
static inline JSONObjectValue JSONObjectValue_CreateRealNumber(double value)
{
    return (JSONObjectValue){ .Type = JSONValueType_RealNumber, .Value.RealNumber = value };
}

/**
 * @brief Create a JSONObjectValue representing JSON `null`.
 * @returns A JSONObjectValue of type JSONValueType_Null.
 */
static inline JSONObjectValue JSONObjectValue_CreateNull()
{
    return (JSONObjectValue){ .Type = JSONValueType_Null, .Value.Null = NULL };
}

/**
 * @brief Wrap a compound into a JSONObjectValue tagged JSONValueType_Compound.
 *
 * Builds the tagged union only; the compound is not copied. The referenced compound must be borrowed from
 * the pool that will own the containing structure and remains pool-owned.
 * @param value Borrowed, pool-owned JSONCompound to reference. May be NULL, but a NULL compound cannot be
 *        serialized.
 * @returns A JSONObjectValue of type JSONValueType_Compound referencing @p value.
 */
static inline JSONObjectValue JSONObjectValue_CreateCompound(JSONCompound* value)
{
    return (JSONObjectValue){ .Type = JSONValueType_Compound, .Value.Compound = value };
}

/**
 * @brief Wrap an array into a JSONObjectValue tagged JSONValueType_Array.
 *
 * Builds the tagged union only; the array is not copied. The referenced array must be borrowed from the
 * pool that will own the containing structure and remains pool-owned.
 * @param value Borrowed, pool-owned JSONArray to reference. May be NULL, but a NULL array cannot be
 *        serialized.
 * @returns A JSONObjectValue of type JSONValueType_Array referencing @p value.
 */
static inline JSONObjectValue JSONObjectValue_CreateArray(JSONArray* value)
{
    return (JSONObjectValue){ .Type = JSONValueType_Array, .Value.Array = value };
}


/**
 * @brief Serialize a JSON value tree to UTF-8 text and append it to a byte buffer.
 *
 * Writes the textual JSON for @p value (recursively, including nested compounds and arrays) by appending
 * raw bytes to the end of @p destination's current contents; existing contents are kept (unlike string
 * writers, this does not drop a trailing null first). With JSONSerializeFlags_PrettyPrinted the output is
 * indented and multi-line, otherwise compact. With JSONSerializeFlags_NullTerminated a single null byte is
 * appended after the text. The pool is not consulted and @p value is not modified or released.
 * @param value The value tree to serialize. Must not be NULL. Any reachable String/Compound/Array pointer
 *        must be non-NULL, and real numbers must be finite.
 * @param destination [out] Byte buffer (element size 1) the serialized text is appended to. Must not be
 *        NULL and must be a byte buffer.
 * @param flags Bitwise-OR of JSONSerializeFlags controlling pretty-printing and null termination.
 * @returns Success when the full value was written. Raises ErrorCode_IllegalArgument if @p value is NULL or
 *          @p destination is NULL/not a byte buffer; ErrorCode_Serialize for unserializable content (NULL
 *          String/Compound/Array, non-finite real number); ErrorCode_BufferTooSmall if @p destination
 *          cannot grow; ErrorCode_InvalidOperation for an unknown value type.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSON_Serialize(JSONObjectValue* value, GenericBuffer* destination, JSONSerializeFlags flags);

/**
 * @brief Parse a complete UTF-8 JSON document from a byte buffer into a JSON value tree.
 *
 * Parses the entire contents of @p source (a single top-level JSON value) and writes the result to
 * @p outValue. Every compound, array and string produced is borrowed from @p pool, so the returned tree is
 * owned by @p pool and must eventually be released back to it (e.g. via JSONObjectPool_ReturnValue, or by
 * deconstructing the pool). The whole input must be consumed apart from surrounding whitespace; trailing
 * non-whitespace content is an error. On any failure @p outValue is set to JSONValueType_None and any
 * partially built tree is returned to the pool, so the caller never owns a partial result.
 * @param pool The object pool that will own all produced structures. Must not be NULL.
 * @param source [in] Byte buffer (element size 1) holding the JSON text to parse. Must not be NULL and must
 *        be a byte buffer; it is read, not modified. A trailing null terminator is not required.
 * @param outValue [out] Receives the parsed root value on success; set to JSONValueType_None on failure.
 *        Must not be NULL.
 * @returns Success when the entire input parsed to one value. Raises ErrorCode_IllegalArgument if @p pool
 *          or @p outValue is NULL or @p source is NULL/not a byte buffer; ErrorCode_InvalidJSON for
 *          malformed syntax or unexpected trailing content (the message includes the byte offset).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSON_Deserialize(JSONObjectPool* pool, GenericBuffer* source, JSONObjectValue* outValue);


/**
 * @brief Insert or overwrite the value stored under a key in a compound.
 *
 * If @p key is absent it is added; if already present its value is overwritten. On overwrite the previous
 * value is first returned to the pool (recursively releasing any nested string/compound/array it held).
 * The key bytes are copied into pool-owned storage on first insertion, so the caller's @p key buffer does
 * not need to outlive the call. @p value is copied by value (a borrowed handle); ownership of the
 * referenced string/compound/array effectively transfers to this compound until it is overwritten,
 * removed, or the compound is cleared/returned.
 * @param self The compound to modify. Must not be NULL.
 * @param key NUL-terminated UTF-8 key. Must not be NULL. Must not contain an embedded U+0000 byte.
 * @param value [in] The value to store. Must not be NULL. Any referenced structure must be borrowed from
 *        the same pool that owns @p self.
 * @returns Success on insert or overwrite. Raises ErrorCode_IllegalArgument if @p self, @p key or @p value
 *          is NULL; ErrorCode_InvalidJSON if the key contains U+0000; ErrorCode_BufferTooSmall if key
 *          storage cannot grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONCompound_Set(JSONCompound* self, const unsigned char* key, JSONObjectValue* value);

/**
 * @brief Look up a key in a compound, requiring it to be present.
 *
 * Copies the stored value into @p outValue. The returned value borrows pool-owned storage (any
 * string/compound/array pointer remains owned by the pool / the compound); the caller must not return it to
 * the pool while it is still held by the compound.
 * @param self The compound to query. Must not be NULL.
 * @param key NUL-terminated UTF-8 key to find. Must not be NULL.
 * @param outValue [out] Receives the stored value on success. Must not be NULL.
 * @returns Success if found. Raises ErrorCode_IllegalArgument if @p self, @p key or @p outValue is NULL;
 *          ErrorCode_InvalidOperation if no entry exists for @p key.
 */
Error JSONCompound_Get(JSONCompound* self, const unsigned char* key, JSONObjectValue* outValue);

/**
 * @brief Look up a key in a compound without treating absence as an error.
 *
 * Like JSONCompound_Get but reports presence via @p outWasFound instead of failing when the key is missing.
 * When not found, @p outValue is set to JSONValueType_None and the call still succeeds. A found value
 * borrows pool-owned storage (see JSONCompound_Get).
 * @param self The compound to query. Must not be NULL.
 * @param key NUL-terminated UTF-8 key to find. Must not be NULL.
 * @param outValue [out] Receives the stored value when found, else JSONValueType_None. Must not be NULL.
 * @param outWasFound [out] Set to true if the key was present, false otherwise. Must not be NULL.
 * @returns Success whether or not the key was found. Raises ErrorCode_IllegalArgument if @p self, @p key,
 *          @p outValue or @p outWasFound is NULL.
 */
Error JSONCompound_GetOptional(JSONCompound* self,
    const unsigned char* key, 
    JSONObjectValue* outValue,
    bool* outWasFound);

/**
 * @brief Look up a required key and assert the stored value has an expected type.
 *
 * Behaves like JSONCompound_Get, then additionally fails if the found value's type is not @p expectedType.
 * The returned value borrows pool-owned storage (see JSONCompound_Get).
 * @param self The compound to query. Must not be NULL.
 * @param key NUL-terminated UTF-8 key to find. Must not be NULL.
 * @param expectedType The JSONValueType the value must have.
 * @param outValue [out] Receives the stored value on success. Must not be NULL.
 * @returns Success if found and of the expected type. Raises ErrorCode_IllegalArgument for NULL @p self,
 *          @p key or @p outValue; ErrorCode_InvalidOperation if the key is absent or the value's type does
 *          not equal @p expectedType.
 */
Error JSONCompound_GetVerified(JSONCompound* self, 
    const unsigned char* key,
    JSONValueType expectedType,
    JSONObjectValue* outValue);

/**
 * @brief Optionally look up a key, and if present assert its value has an expected type.
 *
 * Combines the optional and verified behaviors: a missing key is not an error (@p outWasFound is false,
 * @p outValue becomes JSONValueType_None), but if the key IS present and its type differs from
 * @p expectedType the call fails. A found value borrows pool-owned storage (see JSONCompound_Get).
 * @param self The compound to query. Must not be NULL.
 * @param key NUL-terminated UTF-8 key to find. Must not be NULL.
 * @param expectedType The JSONValueType the value must have when present.
 * @param outValue [out] Receives the stored value when found, else JSONValueType_None. Must not be NULL.
 * @param outWasFound [out] Set to true if the key was present, false otherwise. Must not be NULL.
 * @returns Success if the key is absent, or present with the expected type. Raises
 *          ErrorCode_IllegalArgument for NULL @p self, @p key, @p outValue or @p outWasFound;
 *          ErrorCode_InvalidOperation if the key is present but of a different type.
 */
Error JSONCompound_GetOptionalVerified(JSONCompound* self,
    const unsigned char* key,
    JSONValueType expectedType, 
    JSONObjectValue* outValue, 
    bool* outWasFound);

/**
 * @brief Remove every entry from a compound, returning all contained values to the pool.
 *
 * Each stored value is returned to the owning pool (recursively releasing nested strings/compounds/arrays)
 * before the entry map is emptied. After success the compound has zero entries but remains valid and
 * borrowed. Registered key storage is not freed here (it is reclaimed when the pool is deconstructed).
 * @param self The compound to clear. Must not be NULL.
 * @returns Success when the compound is emptied. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_BufferTooSmall if an enumerator over the values cannot be created.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONCompound_Clear(JSONCompound* self);

/**
 * @brief Remove a single entry by key, returning its value to the pool.
 *
 * The entry's value is returned to the owning pool (recursively releasing any nested structure) and the
 * key/value pair is removed.
 * @param self The compound to modify. Must not be NULL.
 * @param key NUL-terminated UTF-8 key of the entry to remove. Must not be NULL.
 * @returns Success if an entry was removed. Raises ErrorCode_IllegalArgument if @p self or @p key is NULL;
 *          ErrorCode_InvalidOperation if no entry exists for @p key.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONCompound_Remove(JSONCompound* self, const unsigned char* key);

/**
 * @brief Get the number of key/value entries currently in a compound.
 * @param self The compound to query. May be NULL.
 * @returns The entry count, or 0 if @p self is NULL.
 */
size_t JSONCompound_GetEntryCount(JSONCompound* self);

/**
 * @brief Get a read-only collection that iterates the compound's entries as JSONCompoundEntry values.
 *
 * The returned collection is owned by @p self (not a new allocation); do not free it. Enumerated entries
 * borrow pool-owned key/value storage and are valid only until the compound is modified. Iteration order is
 * unspecified.
 * @param self The compound to iterate. May be NULL.
 * @returns A borrowed ICollection of JSONCompoundEntry, or NULL if @p self is NULL.
 */
ICollection* JSONCompound_GetEntryCollection(JSONCompound* self);

/**
 * @brief Get a read-only collection that iterates the compound's values as JSONObjectValue.
 *
 * The returned collection is owned by @p self; do not free it. Enumerated values borrow pool-owned storage
 * and are valid only until the compound is modified. Iteration order is unspecified.
 * @param self The compound to iterate. May be NULL.
 * @returns A borrowed ICollection of JSONObjectValue, or NULL if @p self is NULL.
 */
ICollection* JSONCompound_GetValueCollection(JSONCompound* self);

/**
 * @brief Get a read-only collection that iterates the compound's keys as `unsigned char*` strings.
 *
 * The returned collection is owned by @p self; do not free it. Enumerated keys are borrowed, NUL-terminated
 * UTF-8 strings owned by the pool and are valid only until the compound is modified. Iteration order is
 * unspecified.
 * @param self The compound to iterate. May be NULL.
 * @returns A borrowed ICollection of `unsigned char*` keys, or NULL if @p self is NULL.
 */
ICollection* JSONCompound_GetKeyCollection(JSONCompound* self);



/**
 * @brief Overwrite the element at an index, returning the previous element to the pool.
 *
 * The existing element at @p index is returned to the owning pool (recursively releasing any nested
 * structure) and replaced with a copy of @p value. @p value must belong to the same pool as @p self;
 * ownership of its referenced structure effectively transfers to the array.
 * @param self The array to modify. Must not be NULL.
 * @param index Zero-based position to overwrite. Must be within bounds.
 * @param value [in] The replacement value. Must not be NULL and must reference the same pool as @p self.
 * @returns Success on replacement. Raises ErrorCode_IllegalArgument if @p self or @p value is NULL;
 *          ErrorCode_InvalidOperation if @p value references a different pool; ErrorCode_InvalidOperation
 *          if @p index is out of range (no element found).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONArray_Replace(JSONArray* self, size_t index, JSONObjectValue* value);

/**
 * @brief Append a value to the end of an array.
 *
 * Stores a copy of @p value as the new last element. @p value must belong to the same pool as @p self;
 * ownership of its referenced structure effectively transfers to the array.
 * @param self The array to modify. Must not be NULL.
 * @param value [in] The value to append. Must not be NULL and must reference the same pool as @p self.
 * @returns Success on append. Raises ErrorCode_IllegalArgument if @p self or @p value is NULL;
 *          ErrorCode_InvalidOperation if @p value references a different pool.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONArray_Add(JSONArray* self, JSONObjectValue* value);

/**
 * @brief Insert a value at an index, shifting later elements toward the end.
 *
 * Stores a copy of @p value at @p index. @p value must belong to the same pool as @p self; ownership of its
 * referenced structure effectively transfers to the array.
 * @param self The array to modify. Must not be NULL.
 * @param index Zero-based position at which to insert (an index equal to the current count appends).
 * @param value [in] The value to insert. Must not be NULL and must reference the same pool as @p self.
 * @returns Success on insertion. Raises ErrorCode_IllegalArgument if @p self or @p value is NULL;
 *          ErrorCode_InvalidOperation if @p value references a different pool. Propagates an out-of-range
 *          error from the backing list if @p index exceeds the allowed range.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONArray_Insert(JSONArray* self, size_t index, JSONObjectValue* value);

/**
 * @brief Read the element at an index, requiring it to exist.
 *
 * Copies the element into @p outValue. The returned value borrows pool-owned storage (any nested pointer
 * remains owned by the array/pool); do not return it to the pool while it is still held by the array.
 * @param self The array to query. Must not be NULL.
 * @param index Zero-based position to read.
 * @param outValue [out] Receives the element on success. Must not be NULL.
 * @returns Success if the index is in range. Raises ErrorCode_IllegalArgument if @p self or @p outValue is
 *          NULL; ErrorCode_InvalidOperation if @p index is out of range.
 */
Error JSONArray_Get(JSONArray* self, size_t index, JSONObjectValue* outValue);

/**
 * @brief Read an element by index without treating an out-of-range index as an error.
 *
 * Like JSONArray_Get but reports presence via @p outWasFound. When the index is out of range @p outValue is
 * set to JSONValueType_None and the call still succeeds. A found value borrows pool-owned storage.
 * @param self The array to query. Must not be NULL.
 * @param index Zero-based position to read.
 * @param outValue [out] Receives the element when in range, else JSONValueType_None. Must not be NULL.
 * @param outWasFound [out] Set to true if @p index was in range, false otherwise. Must not be NULL.
 * @returns Success whether or not the index was in range. Raises ErrorCode_IllegalArgument if @p self,
 *          @p outValue or @p outWasFound is NULL.
 */
Error JSONArray_GetOptional(JSONArray* self, size_t index, JSONObjectValue* outValue, bool* outWasFound);

/**
 * @brief Read a required element and assert it has an expected type.
 *
 * Behaves like JSONArray_Get, then fails if the element's type is not @p expectedType. The returned value
 * borrows pool-owned storage.
 * @param self The array to query. Must not be NULL.
 * @param index Zero-based position to read.
 * @param expectedType The JSONValueType the element must have.
 * @param outValue [out] Receives the element on success. Must not be NULL.
 * @returns Success if in range and of the expected type. Raises ErrorCode_IllegalArgument if @p self or
 *          @p outValue is NULL; ErrorCode_InvalidOperation if @p index is out of range or the element's
 *          type does not equal @p expectedType.
 */
Error JSONArray_GetVerified(JSONArray* self, size_t index, JSONValueType expectedType, JSONObjectValue* outValue);

/**
 * @brief Optionally read an element, and if it exists assert it has an expected type.
 *
 * Combines optional and verified behaviors: an out-of-range index is not an error (@p outWasFound false,
 * @p outValue JSONValueType_None), but an in-range element whose type differs from @p expectedType fails.
 * A found value borrows pool-owned storage.
 * @param self The array to query. Must not be NULL.
 * @param index Zero-based position to read.
 * @param expectedType The JSONValueType the element must have when present.
 * @param outValue [out] Receives the element when in range, else JSONValueType_None. Must not be NULL.
 * @param outWasFound [out] Set to true if @p index was in range, false otherwise. Must not be NULL.
 * @returns Success if the index is out of range, or in range with the expected type. Raises
 *          ErrorCode_IllegalArgument if @p self, @p outValue or @p outWasFound is NULL;
 *          ErrorCode_InvalidOperation if the element is present but of a different type.
 */
Error JSONArray_GetOptionalVerified(JSONArray* self, 
    size_t index,
    JSONValueType expectedType,
    JSONObjectValue* outValue,
    bool* outWasFound);

/**
 * @brief Remove every element from an array, returning each to the pool.
 *
 * Each element is returned to the owning pool (recursively releasing nested structures) before the array is
 * emptied. After success the array has zero elements but remains valid and borrowed.
 * @param self The array to clear. Must not be NULL.
 * @returns Success when the array is emptied. Raises ErrorCode_IllegalArgument if @p self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONArray_Clear(JSONArray* self);

/**
 * @brief Remove the element at an index, returning it to the pool and shifting later elements down.
 *
 * The removed element is returned to the owning pool (recursively releasing any nested structure).
 * @param self The array to modify. Must not be NULL.
 * @param index Zero-based position to remove. Must be within bounds.
 * @returns Success on removal. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_InvalidOperation if @p index is out of range.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONArray_RemoveAt(JSONArray* self, size_t index);

/**
 * @brief Get the number of elements currently in an array.
 * @param self The array to query. May be NULL.
 * @returns The element count, or 0 if @p self is NULL.
 */
size_t JSONArray_GetElementCount(JSONArray* self);

/**
 * @brief Get a read-only collection that iterates the array's elements as JSONArrayIndexedValue values.
 *
 * The returned collection is owned by @p self (not a new allocation); do not free it. Enumerated values
 * carry the element index plus a value that borrows pool-owned storage, and are valid only until the array
 * is modified. Iteration proceeds in ascending index order.
 * @param self The array to iterate. May be NULL.
 * @returns A borrowed ICollection of JSONArrayIndexedValue, or NULL if @p self is NULL.
 */
ICollection* JSONArray_GetElementCollection(JSONArray* self);


/**
 * @brief Allocate and initialize a new JSON object pool.
 *
 * Creates the heap-allocated pool together with its internal sub-pools for compounds, arrays and string
 * buffers, and storage tracking duplicated keys. The resulting pool must later be released with
 * JSONObjectPool_Deconstruct.
 * @param outPool [out] Receives the new pool on success; set to NULL first and left NULL on failure. Must
 *        not be NULL.
 * @returns Success when the pool is created. Raises ErrorCode_IllegalArgument if @p outPool is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONObjectPool_Create(JSONObjectPool** outPool);

/**
 * @brief Destroy a JSON object pool and free everything it owns.
 *
 * Tears down all three sub-pools (deconstructing every pooled compound, array and string buffer ever
 * created, including any still borrowed) and frees all duplicated key storage and the pool itself. This
 * invalidates every JSONCompound, JSONArray, string buffer and JSONObjectValue obtained from the pool.
 * Teardown is best-effort: it continues past individual failures and reports them together in one error.
 * @param self The pool to destroy. Must not be NULL.
 * @returns Success if teardown completed cleanly. Raises ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_Deconstruct if one or more sub-objects failed to deconstruct (the message states how
 *          many and concatenates their messages).
 */
Error JSONObjectPool_Deconstruct(JSONObjectPool* self);

/**
 * @brief Borrow an empty compound from the pool.
 *
 * Returns a cleared JSONCompound owned by @p self. The caller must eventually give it back with
 * JSONObjectPool_ReturnCompound (or via JSONObjectPool_ReturnValue, or by storing it in a parent container
 * that is later released), or it is reclaimed when the pool is deconstructed.
 * @param self The pool to borrow from. Must not be NULL.
 * @param outCompound [out] Receives the borrowed compound; set to NULL first and left NULL on failure. Must
 *        not be NULL.
 * @returns Success when a compound is provided. Raises ErrorCode_IllegalArgument if @p self or
 *          @p outCompound is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONObjectPool_BorrowCompound(JSONObjectPool* self, JSONCompound** outCompound);

/**
 * @brief Borrow an empty array from the pool.
 *
 * Returns a cleared JSONArray owned by @p self. The caller must eventually give it back with
 * JSONObjectPool_ReturnArray (or via JSONObjectPool_ReturnValue, or by storing it in a parent container
 * that is later released), or it is reclaimed when the pool is deconstructed.
 * @param self The pool to borrow from. Must not be NULL.
 * @param outArray [out] Receives the borrowed array; set to NULL first and left NULL on failure. Must not
 *        be NULL.
 * @returns Success when an array is provided. Raises ErrorCode_IllegalArgument if @p self or @p outArray is
 *          NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONObjectPool_BorrowArray(JSONObjectPool* self, JSONArray** outArray);

/**
 * @brief Borrow an empty UTF-8 string buffer from the pool.
 *
 * Returns a cleared byte GenericBuffer (element size 1) owned by @p self, intended to hold a JSON string
 * value's UTF-8 bytes. The caller fills it, wraps it with JSONObjectValue_CreateString, and must eventually
 * give it back with JSONObjectPool_ReturnString (or via JSONObjectPool_ReturnValue / a parent container),
 * or it is reclaimed when the pool is deconstructed.
 * @param self The pool to borrow from. Must not be NULL.
 * @param outStringBuffer [out] Receives the borrowed buffer; set to NULL first and left NULL on failure.
 *        Must not be NULL.
 * @returns Success when a buffer is provided. Raises ErrorCode_IllegalArgument if @p self or
 *          @p outStringBuffer is NULL; ErrorCode_BufferTooSmall if the borrowed buffer cannot be cleared.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONObjectPool_BorrowString(JSONObjectPool* self, GenericBuffer** outStringBuffer);

/**
 * @brief Return a previously borrowed compound to the pool.
 *
 * The compound must have been borrowed from @p self. When @p includeNestedStructures is true the compound
 * is first cleared, recursively returning every value it holds (nested strings/compounds/arrays) to the
 * pool; when false only the compound shell is returned and the caller is responsible for any contents
 * (typically because they were moved elsewhere), otherwise those nested objects leak until pool teardown.
 * After this call the @p compound pointer must not be used.
 * @param self The owning pool. Must not be NULL.
 * @param compound The compound to return. Must not be NULL and must belong to @p self.
 * @param includeNestedStructures Whether to also release the compound's contained values.
 * @returns Success when returned. Raises ErrorCode_IllegalArgument if @p self or @p compound is NULL;
 *          ErrorCode_InvalidOperation if @p compound does not belong to @p self.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONObjectPool_ReturnCompound(JSONObjectPool* self, JSONCompound* compound, bool includeNestedStructures);

/**
 * @brief Return a previously borrowed array to the pool.
 *
 * The array must have been borrowed from @p self. When @p includeNestedStructures is true the array is
 * first cleared, recursively returning every element it holds to the pool; when false only the array shell
 * is returned and the caller owns any contents (otherwise those nested objects leak until pool teardown).
 * After this call the @p array pointer must not be used.
 * @param self The owning pool. Must not be NULL.
 * @param array The array to return. Must not be NULL and must belong to @p self.
 * @param includeNestedStructures Whether to also release the array's contained elements.
 * @returns Success when returned. Raises ErrorCode_IllegalArgument if @p self or @p array is NULL;
 *          ErrorCode_InvalidOperation if @p array does not belong to @p self.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONObjectPool_ReturnArray(JSONObjectPool* self, JSONArray* array, bool includeNestedStructures);

/**
 * @brief Return a previously borrowed string buffer to the pool.
 *
 * The buffer must have been borrowed from @p self (ownership is verified). It is cleared and recycled.
 * After this call the @p stringBuffer pointer must not be used.
 * @param self The owning pool. Must not be NULL.
 * @param stringBuffer The string buffer to return. Must not be NULL and must belong to @p self.
 * @returns Success when returned. Raises ErrorCode_IllegalArgument if @p self or @p stringBuffer is NULL;
 *          ErrorCode_InvalidOperation if the buffer does not belong to @p self; ErrorCode_BufferTooSmall if
 *          the buffer cannot be cleared.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONObjectPool_ReturnString(JSONObjectPool* self, GenericBuffer* stringBuffer);

/**
 * @brief Return whatever object a JSONObjectValue references to the pool, by its type.
 *
 * Dispatches on @p value's type: a String, Compound or Array is returned to @p self (compounds and arrays
 * are returned WITH their nested contents, as if @p includeNestedStructures were true); scalar types
 * (Boolean/Integer/RealNumber/Null) and None own nothing and are accepted as no-ops. On success @p value is
 * reset to JSONValueType_None so the now-dangling pointer cannot be reused. This is the recommended way to
 * release a value obtained from the parser or extracted from a container.
 * @param self The owning pool. Must not be NULL.
 * @param value [in,out] The value whose referenced object is released; reset to JSONValueType_None on
 *        success. Must not be NULL. Any referenced object must belong to @p self.
 * @returns Success when the referenced object (if any) is returned. Raises ErrorCode_IllegalArgument if
 *          @p self or @p value is NULL; ErrorCode_InvalidOperation if a referenced object does not belong
 *          to @p self or the value's type is unrecognized.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error JSONObjectPool_ReturnValue(JSONObjectPool* self, JSONObjectValue* value);
