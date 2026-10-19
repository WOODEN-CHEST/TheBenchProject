#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRGHDF.h"
#include "wr/WRJSON.h"
#include "DataValue.h"
#include "NamespacedKey.h"


/**
 * @file DataMap.h
 * @brief A map from namespaced keys to data values: the tags and custom data of a world object, and later the game's
 *        global variables (the scoreboard replacement).
 *
 * Each entry is a key (an id of the map's KeyRegistry) and a DataValue. A **tag** is an entry of type DataType_Tag:
 * presence only. One map holds both, so tags and data share one system, one save format and one query.
 *
 * LIFETIME. Maps live on the heap (DataMap_Create, DataMap_Clone, the readers) and are released with DataMap_Destroy.
 * The functions that only read take NULL as an empty map, so an owner can keep NULL until the first entry (world
 * objects do). A map borrows its key registry, which must outlive it.
 *
 * LIMITS. The map counts the bytes its entries take (DataMap_GetByteSize: per entry its key's text, 1 and
 * DataValue_GetByteSize of the value), so an owner can bound it with a DataMapLimits: the number of entries and the
 * bytes. The map itself does not know any limit; the setters and readers take the owner's.
 *
 * FILES. JSON writes a map as two keys of its owner's compound, the tags and the values: `"tags": ["town:street_lamp",
 * "game:office"]` and `"data": {"town:visits": 3, "town:owner": "Bob"}` (DataValue.h has the value forms). GHDF
 * writes it as a Compound array of entry compounds: entry 1 the key's full text, then the value's entries (2 its type,
 * 3 its value, 4 a list's element type), a tag having no value. Both write the entries in the order of their keys'
 * text, so the files do not depend on interning order. Readers accept keys without a namespace in JSON (the default
 * namespace is applied) and only full keys in GHDF.
 *
 * Not thread-safe.
 */


// Types.
/**
 * @brief One entry of a map: a key and its value (a tag for presence only).
 */
typedef struct DataMapEntryStruct
{
    /** @brief The key: an id of the map's KeyRegistry. */
    uint32_t Key;
    /** @brief The value; DataType_Tag for a tag. Owned by the map. */
    DataValue Value;
} DataMapEntry;

/**
 * @brief The most a map may hold. An owner's policy: object data has its own, global variables will have theirs.
 */
typedef struct DataMapLimitsStruct
{
    /** @brief The most entries (tags included). */
    size_t MaxEntryCount;
    /** @brief The most bytes the entries may count (DataMap_GetByteSize). */
    size_t MaxByteSize;
} DataMapLimits;

/**
 * @brief A map of keys to values, sorted by key id. Created on the heap; reach its fields through the functions.
 */
typedef struct DataMapStruct
{
    /** @brief The registry its keys come from; borrowed. */
    KeyRegistry* _keys;
    /** @brief Owned array of entries, sorted by key id. */
    DataMapEntry* _entries;
    /** @brief The number of entries. */
    uint32_t _count;
    /** @brief The number of entries _entries has room for. */
    uint32_t _capacity;
    /** @brief The bytes the entries count (see the file comment). */
    size_t _byteSize;
} DataMap;


// Functions: lifetime.
/**
 * @brief Creates an empty map on the heap.
 * @param keys The registry its keys come from; borrowed, must outlive the map. Must not be NULL.
 * @param outMap [out] Receives the map (release it with DataMap_Destroy). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL.
 */
Error DataMap_Create(KeyRegistry* keys, DataMap** outMap);

/**
 * @brief Releases a map and every value in it.
 * @param map The map; NULL does nothing.
 */
void DataMap_Destroy(DataMap* map);

/**
 * @brief Returns a deep copy of a map (same registry), or NULL for NULL or an empty map.
 * @param source The map, or NULL.
 * @returns The copy (release it with DataMap_Destroy), or NULL.
 */
DataMap* DataMap_Clone(const DataMap* source);


// Functions: reading.
/**
 * @brief Returns the number of entries, tags included.
 * @param map The map; NULL is empty.
 * @returns The count.
 */
static inline size_t DataMap_GetCount(const DataMap* map)
{
    return (map != NULL) ? map->_count : 0U;
}

/**
 * @brief Returns the bytes the entries count against a limit (see the file comment).
 * @param map The map; NULL is empty.
 * @returns The byte count.
 */
static inline size_t DataMap_GetByteSize(const DataMap* map)
{
    return (map != NULL) ? map->_byteSize : 0U;
}

/**
 * @brief Returns the registry the map's keys come from.
 * @param map The map; must not be NULL.
 * @returns The registry (borrowed).
 */
static inline KeyRegistry* DataMap_GetKeys(const DataMap* map)
{
    return map->_keys;
}

/**
 * @brief Returns an entry by index, for iteration (the entries are sorted by key id).
 * @param map The map; NULL is empty.
 * @param index The index, below DataMap_GetCount.
 * @returns The entry (owned by the map: valid until the map changes), or NULL when out of range.
 */
const DataMapEntry* DataMap_GetEntry(const DataMap* map, size_t index);

/**
 * @brief Finds the value of a key (a binary search).
 * @param map The map; NULL is empty.
 * @param key The key id.
 * @returns The value (owned by the map: valid until the map changes; a tag for a tag), or NULL when absent.
 */
const DataValue* DataMap_Find(const DataMap* map, uint32_t key);

/**
 * @brief Reports whether the map holds @p key as a tag (an entry of type DataType_Tag).
 * @param map The map; NULL is empty.
 * @param key The key id.
 * @returns true for a tag; false when absent or holding a value.
 */
bool DataMap_HasTag(const DataMap* map, uint32_t key);

/**
 * @brief Reports whether two maps hold the same keys with equal values (DataValue_Equals). NULL equals an empty map.
 *        Both must use the same registry (one per program), as keys compare by id.
 * @param a The first map, or NULL.
 * @param b The second map, or NULL.
 * @returns true if equal.
 */
bool DataMap_Equals(const DataMap* a, const DataMap* b);

/**
 * @brief Returns the bytes an entry counts: its key's text, 1 and the value's DataValue_GetByteSize.
 * @param keys The registry the key and the value's keys come from; must not be NULL.
 * @param key The key id.
 * @param value The value; must not be NULL.
 * @returns The byte count.
 */
size_t DataMap_GetEntryByteSize(const KeyRegistry* keys, uint32_t key, const DataValue* value);

/**
 * @brief Checks a map against limits.
 * @param map The map; NULL is empty.
 * @param limits The limits; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p limits is NULL; ErrorCode_InvalidOperation naming the limit passed.
 */
Error DataMap_CheckLimits(const DataMap* map, const DataMapLimits* limits);


// Functions: changing.
/**
 * @brief Sets a key to a copy of a value (a tag for a tag), replacing what it held.
 *
 * The value is validated (DataValue_Validate, with the map's registry) and copied; the caller keeps its own.
 * @param map The map; must not be NULL.
 * @param key The key; an id of the map's registry.
 * @param value The value; must not be NULL.
 * @param limits The limits the map must stay within, or NULL for none.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL or @p key is not an id of the registry; the
 *          validation error (ErrorCode_ArgumentOutOfRange); ErrorCode_InvalidOperation when the map would pass a
 *          limit (nothing changes then).
 */
Error DataMap_Set(DataMap* map, uint32_t key, const DataValue* value, const DataMapLimits* limits);

/**
 * @brief As DataMap_Set, but MOVES the value in: on success the map owns it and @p value becomes a tag.
 *
 * The readers build maps this way, without a copy per value. The value is not validated: it must come from a
 * validated source (a reader checks every rule while reading).
 * @param map The map; must not be NULL.
 * @param key The key; an id of the map's registry.
 * @param value The value; must not be NULL. Still the caller's on failure.
 * @param limits The limits, or NULL for none.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL or @p key is not an id of the registry;
 *          ErrorCode_InvalidOperation when the map would pass a limit.
 */
Error DataMap_Adopt(DataMap* map, uint32_t key, DataValue* value, const DataMapLimits* limits);

/**
 * @brief Removes a key and releases its value.
 * @param map The map; NULL is empty.
 * @param key The key id.
 * @returns true if the key was there.
 */
bool DataMap_Remove(DataMap* map, uint32_t key);


// Functions: files.
/**
 * @brief Writes a map's tags and values as two keys of a JSON compound (see the file comment).
 *
 * Validates every value first. A part with no entries is left out unless @p areEmptyPartsWritten.
 * @param map The map; NULL is empty.
 * @param pool The pool owning @p parent; must not be NULL.
 * @param parent The compound to write the two keys into (other keys stay); must not be NULL.
 * @param tagsKey The key of the tag list, e.g. u8"tags"; must not be NULL.
 * @param valuesKey The key of the values, e.g. u8"data"; must not be NULL.
 * @param areEmptyPartsWritten true writes `[]` and `{}` for a part without entries.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; a validation error; the errors of building the
 *          JSON. On a failure after validation @p parent may hold one of the keys: discard it.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error DataMap_WriteJSON(const DataMap* map, JSONObjectPool* pool, JSONCompound* parent, const unsigned char* tagsKey,
    const unsigned char* valuesKey, bool areEmptyPartsWritten);

/**
 * @brief Reads a map from the two keys of a JSON compound, interning its keys.
 *
 * Either key may be absent. Every rule is checked while reading: keys (a key listed twice, in either part, is
 * refused), values, depth, the limits (a value's bytes are counted while it is read, so one too large for what the
 * limits leave stops where they run out). Messages name where: "tags[2]: ..." or "data.town:inventory[3]: ...". All or
 * nothing: on failure the registry forgets the keys the read interned (KeyRegistry_TruncateTo).
 * @param parent The compound holding the two keys; must not be NULL.
 * @param tagsKey The key of the tag list; must not be NULL.
 * @param valuesKey The key of the values; must not be NULL.
 * @param keys The registry to intern keys in; must not be NULL.
 * @param defaultNamespace The namespace of keys written without one; NULL means NAMESPACED_KEY_DEFAULT_NAMESPACE.
 * @param limits The limits, or NULL for none.
 * @param outMap [out] Receives the map, or NULL when both parts are absent or empty (release it with
 *        DataMap_Destroy). Must not be NULL. NULL on failure.
 * @param outFoundKeyCount [out] Receives how many of the two keys @p parent holds (0 to 2); may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_InvalidJSON naming the problem.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error DataMap_ReadJSON(JSONCompound* parent, const unsigned char* tagsKey, const unsigned char* valuesKey,
    KeyRegistry* keys, const unsigned char* defaultNamespace, const DataMapLimits* limits, DataMap** outMap,
    size_t* outFoundKeyCount);

/**
 * @brief Writes a map as a GHDF Compound array of entry compounds (see the file comment).
 *
 * Validates every value first.
 * @param map The map; NULL is empty (an empty array).
 * @param pool The pool every written array, compound and string is borrowed from; must not be NULL.
 * @param outArray [out] Receives the array, owned by the caller (store it in a compound or return it to @p pool).
 *        Must not be NULL. Nothing stays borrowed on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; a validation error; the errors of building the
 *          GHDF tree.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error DataMap_WriteGHDF(const DataMap* map, GHDFObjectPool* pool, GHDFArray** outArray);

/**
 * @brief Reads a map from a GHDF Compound array written by DataMap_WriteGHDF, interning its keys.
 *
 * Every rule is checked while reading, as for JSON (the limits too, while a value is read); keys must be in the full
 * form. All or nothing, for the registry too, as for JSON.
 * @param array The array; must not be NULL.
 * @param keys The registry to intern keys in; must not be NULL.
 * @param limits The limits, or NULL for none.
 * @param outMap [out] Receives the map, or NULL for an empty array (release it with DataMap_Destroy). Must not be
 *        NULL. NULL on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_Deserialize naming the problem.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error DataMap_ReadGHDF(GHDFArray* array, KeyRegistry* keys, const DataMapLimits* limits, DataMap** outMap);
