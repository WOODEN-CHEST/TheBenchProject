#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector3 and Color come from raylib: vector3 and color values hold them by value.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRGHDF.h"
#include "wr/WRJSON.h"
#include "NamespacedKey.h"


/**
 * @file DataValue.h
 * @brief One typed value for object data, global variables, selectors, the editor and saves.
 *
 * A DataValue is a tagged value of one of the DataType types: a tag (presence only), bool, int (64-bit), float
 * (64-bit), string, key (a namespaced key), object (a world object id), vector3, color, date_time, list or compound.
 * It is the storable part of the future scripting language's values: the VM adds functions, class instances and
 * fibers on top, and only DataValues can be stored on objects and saved. One integer type and one float type keep
 * script arithmetic free of promotion rules and number suffixes; the binary writer still stores every integer in the
 * smallest GHDF type that holds it.
 *
 * OWNERSHIP. Strings, lists and compounds own heap memory. Build those with DataValue_CreateString, _CreateList and
 * _CreateCompound, copy them with DataValue_Clone, and release every value with DataValue_Deconstruct (a no-op for the
 * other types). A value is a plain struct, but never copy one that owns memory by assignment: two copies would free
 * the same memory. The builders that put a value inside a list or compound (DataValue_ListAdd, DataValue_CompoundSet)
 * MOVE it: on success the container owns it and the source becomes a tag.
 *
 * LISTS hold one element type, fixed when the list is created (any type but tag), and at most
 * DATA_VALUE_MAX_ELEMENT_COUNT elements. COMPOUNDS map names to values, at most DATA_VALUE_MAX_ELEMENT_COUNT entries,
 * kept sorted by name. A name is 1 to DATA_VALUE_MAX_NAME_LENGTH bytes of UTF-8 and must not start with '$', except the
 * optional DATA_VALUE_TYPE_ENTRY_NAME ("$type") entry: a key naming a script-declared struct type (the "custom types",
 * checked against their declaration once the language exists).
 *
 * LIMITS. Values nest at most DATA_VALUE_MAX_DEPTH deep (a scalar has depth 1, a list or compound one more than its
 * deepest element), strings hold at most DATA_VALUE_MAX_STRING_LENGTH bytes. The builders refuse anything past these,
 * so no value deeper than the limit can exist, and the file readers refuse it in the data before building anything.
 * DataValue_Validate checks what the builders cannot (finite floats, keys that exist, dates in the calendar).
 *
 * VALUES IN FILES (DataValue_WriteJSON / _ReadJSON, DataValue_WriteGHDF / _ReadGHDF):
 *
 * | Type      | In memory                     | JSON                                         | GHDF value                     |
 * |-----------|-------------------------------|----------------------------------------------|--------------------------------|
 * | tag       | nothing                       | (only in a map's tag list)                    | none                           |
 * | bool      | bool                          | true / false                                 | Boolean                        |
 * | int       | int64_t                       | an integer: 3                                | Int8/16/32/64, the smallest    |
 * | float     | double, finite                | a fraction or an exponent: 1.5, 2.0          | Float when exact, else Double  |
 * | string    | owned UTF-8                   | "text"                                       | String                         |
 * | key       | uint32_t id                   | {"$key": "town:x"}                           | String (the full key)          |
 * | object    | uint64_t id, 0 = none         | {"$object": 12}                              | UInt8/16/32/64, the smallest   |
 * | vector3   | Vector3                       | {"$vector3": [1, 2, 3]}                       | Float array of 3               |
 * | color     | Color (RGBA)                  | {"$color": "#ffcc00"} ("#rrggbbaa" if not opaque) | UInt8 array of 4          |
 * | date_time | int64_t microseconds          | {"$date_time": "2026-10-18T09:30:00"}        | Int64                          |
 * | list      | owned elements                | [ ... ] (an empty one: {"$list": "int"})     | an array (see below)           |
 * | compound  | owned, sorted entries         | { "name": value, ... }                       | Compound array of entries      |
 *
 * JSON integers and numbers with a fraction or an exponent stay apart: WRJSON keeps them apart when reading, and
 * writes a whole float with its fraction (2.0). {"$float": 2} also reads as a float, for files from tools that drop
 * the ".0" (the writer never uses it). A JSON object with one "$" key is a wrapper; any other object is a compound
 * (its names must not start with '$', except "$type", written as the key's text). A list's elements must all read as
 * its first element's type, and an empty JSON array is refused (its element type would be unknown). Keys in "$key"
 * and "$type" may leave out their namespace: the reader's default namespace is applied.
 *
 * In GHDF a value is written into a compound: entry 2 its type (UInt8, the DataType value), entry 3 its value (absent
 * for a tag), entry 4 a list's element type (UInt8). A compound's entries are a Compound array whose elements also
 * hold entry 1, their name (String). A list is an array of its elements: Boolean, the smallest integer type for ints
 * (unsigned for objects), Float or Double for floats, String for strings and keys, Float (3 per element) for vector3,
 * UInt8 (4 per element) for colors, Int64 for date_times, and a Compound array of value compounds (entries 2 to 4) for
 * lists and compounds. Unknown entry ids are skipped (forward compatibility).
 *
 * Not thread-safe.
 */


// Macros.
/** @brief The deepest a value may nest: a scalar has depth 1, a list or compound one more than its deepest element. */
#define DATA_VALUE_MAX_DEPTH ((size_t)16)
/** @brief The most bytes a string value may hold, without the terminator. */
#define DATA_VALUE_MAX_STRING_LENGTH ((size_t)4096)
/** @brief The most elements of a list, and the most entries of a compound. */
#define DATA_VALUE_MAX_ELEMENT_COUNT ((size_t)4096)
/** @brief The most bytes of a compound entry's name. */
#define DATA_VALUE_MAX_NAME_LENGTH ((size_t)256)
/** @brief The name of a compound's optional entry naming its script-declared struct type (a key value). */
#define DATA_VALUE_TYPE_ENTRY_NAME ((const unsigned char*)u8"$type")


// Types.
/**
 * @brief The type of a DataValue. Saved by its value: never renumber.
 */
typedef enum DataTypeEnum
{
    /** @brief Presence only: an entry with no value. A tag is a map entry of this type. */
    DataType_Tag = 0,
    /** @brief true or false. */
    DataType_Bool = 1,
    /** @brief A 64-bit signed integer: the one integer type. */
    DataType_Int = 2,
    /** @brief A 64-bit float (double), always finite: the one float type. */
    DataType_Float = 3,
    /** @brief UTF-8 text, at most DATA_VALUE_MAX_STRING_LENGTH bytes. */
    DataType_String = 4,
    /** @brief A namespaced key (an id of the program's KeyRegistry): a reference to an asset, a preset, a tag ... */
    DataType_Key = 5,
    /** @brief A world object reference by id (0 = none). It reads as none once that object is removed: ids are never
     *  handed out twice. */
    DataType_Object = 6,
    /** @brief A Vector3 of finite floats. */
    DataType_Vector3 = 7,
    /** @brief An RGBA color. */
    DataType_Color = 8,
    /** @brief A calendar date and time: microseconds since 0001-01-01T00:00:00 (see GameCalendar.h). */
    DataType_DateTime = 9,
    /** @brief A list of values of one element type. */
    DataType_List = 10,
    /** @brief Names -> values, sorted by name. */
    DataType_Compound = 11
} DataType;

/** @brief The number of DataType values (they are 0 to this count - 1). */
#define DATA_TYPE_COUNT 12

/** @brief A list's storage (opaque; reach it through the DataValue_*List* functions). */
typedef struct DataListStruct DataList;
/** @brief A compound's storage (opaque; reach it through the DataValue_*Compound* functions). */
typedef struct DataCompoundStruct DataCompound;

/**
 * @brief A typed value. Build one with the DataValue_From* / DataValue_Create* functions; release it with
 *        DataValue_Deconstruct. See the file comment for ownership.
 */
typedef struct DataValueStruct
{
    /** @brief The type; selects the active member. */
    DataType Type;
    union
    {
        /** @brief DataType_Bool. */
        bool BoolValue;
        /** @brief DataType_Int. */
        int64_t IntValue;
        /** @brief DataType_Float. */
        double FloatValue;
        /** @brief DataType_String: owned, NUL-terminated UTF-8. */
        unsigned char* StringValue;
        /** @brief DataType_Key: a KeyRegistry id. */
        uint32_t KeyValue;
        /** @brief DataType_Object: an object id, 0 = none. */
        uint64_t ObjectValue;
        /** @brief DataType_Vector3. */
        Vector3 Vector3Value;
        /** @brief DataType_Color. */
        Color ColorValue;
        /** @brief DataType_DateTime: microseconds since 0001-01-01T00:00:00. */
        int64_t DateTimeValue;
        /** @brief DataType_List: owned storage. */
        DataList* ListValue;
        /** @brief DataType_Compound: owned storage. */
        DataCompound* CompoundValue;
    };
} DataValue;


// Functions: types.
/**
 * @brief Returns a type's name as files write it, e.g. u8"date_time".
 * @param type The type.
 * @returns A static string; u8"unknown" for a value outside DataType.
 */
const unsigned char* DataType_GetName(DataType type);

/**
 * @brief Finds a type by its name (DataType_GetName).
 * @param name The name's bytes; need not be NUL-terminated. May be NULL only when @p byteCount is 0.
 * @param byteCount The number of bytes.
 * @param outType [out] Receives the type when found. Must not be NULL.
 * @returns true if a type has that name.
 */
bool DataType_TryParseName(const unsigned char* name, size_t byteCount, DataType* outType);


// Functions: values without memory of their own.
/** @brief Makes a tag (no value). @returns The value. */
static inline DataValue DataValue_CreateTag(void)
{
    return (DataValue){ .Type = DataType_Tag, .IntValue = 0 };
}

/** @brief Makes a bool. @param value The value. @returns The value. */
static inline DataValue DataValue_FromBool(bool value)
{
    return (DataValue){ .Type = DataType_Bool, .BoolValue = value };
}

/** @brief Makes an int. @param value The value. @returns The value. */
static inline DataValue DataValue_FromInt(int64_t value)
{
    return (DataValue){ .Type = DataType_Int, .IntValue = value };
}

/** @brief Makes a float (it must be finite to be stored). @param value The value. @returns The value. */
static inline DataValue DataValue_FromFloat(double value)
{
    return (DataValue){ .Type = DataType_Float, .FloatValue = value };
}

/** @brief Makes a key value. @param key A KeyRegistry id (KeyRegistry_Intern). @returns The value. */
static inline DataValue DataValue_FromKey(uint32_t key)
{
    return (DataValue){ .Type = DataType_Key, .KeyValue = key };
}

/** @brief Makes an object reference. @param objectId The object's id, 0 for none. @returns The value. */
static inline DataValue DataValue_FromObject(uint64_t objectId)
{
    return (DataValue){ .Type = DataType_Object, .ObjectValue = objectId };
}

/** @brief Makes a vector3 (its components must be finite to be stored). @param value The value. @returns The value. */
static inline DataValue DataValue_FromVector3(Vector3 value)
{
    return (DataValue){ .Type = DataType_Vector3, .Vector3Value = value };
}

/** @brief Makes a color. @param value The value. @returns The value. */
static inline DataValue DataValue_FromColor(Color value)
{
    return (DataValue){ .Type = DataType_Color, .ColorValue = value };
}

/** @brief Makes a date_time. @param dateTime Microseconds since 0001-01-01T00:00:00 (inside the calendar to be stored).
 *  @returns The value. */
static inline DataValue DataValue_FromDateTime(int64_t dateTime)
{
    return (DataValue){ .Type = DataType_DateTime, .DateTimeValue = dateTime };
}


// Functions: values that own memory.
/**
 * @brief Makes a string value holding a copy of @p text.
 * @param text The NUL-terminated UTF-8 text; must not be NULL.
 * @param outValue [out] Receives the value (release it with DataValue_Deconstruct). Must not be NULL. A tag on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_ArgumentOutOfRange if the text is
 *          longer than DATA_VALUE_MAX_STRING_LENGTH bytes or not valid UTF-8.
 */
Error DataValue_CreateString(const unsigned char* text, DataValue* outValue);

/**
 * @brief Makes a string value from bytes that are not NUL-terminated (a JSON or GHDF string).
 * @param bytes The UTF-8 bytes. May be NULL only when @p byteCount is 0.
 * @param byteCount The number of bytes.
 * @param outValue [out] Receives the value (release it with DataValue_Deconstruct). Must not be NULL. A tag on failure.
 * @returns Success; ErrorCode_IllegalArgument if @p outValue is NULL; ErrorCode_ArgumentOutOfRange if there are more
 *          than DATA_VALUE_MAX_STRING_LENGTH bytes, a NUL byte or invalid UTF-8.
 */
Error DataValue_CreateStringFromBytes(const unsigned char* bytes, size_t byteCount, DataValue* outValue);

/**
 * @brief Makes an empty list of @p elementType.
 * @param elementType The type every element will have; any type but DataType_Tag.
 * @param outValue [out] Receives the list (release it with DataValue_Deconstruct). Must not be NULL. A tag on failure.
 * @returns Success; ErrorCode_IllegalArgument if @p outValue is NULL or @p elementType is a tag or not a type.
 */
Error DataValue_CreateList(DataType elementType, DataValue* outValue);

/**
 * @brief Makes an empty compound.
 * @param outValue [out] Receives the compound (release it with DataValue_Deconstruct). Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p outValue is NULL.
 */
Error DataValue_CreateCompound(DataValue* outValue);

/**
 * @brief Returns a deep copy of a value: strings, lists and compounds are copied, so the two are independent.
 * @param source The value; must not be NULL.
 * @returns The copy (release it with DataValue_Deconstruct). A tag for NULL.
 */
DataValue DataValue_Clone(const DataValue* source);

/**
 * @brief Releases what a value owns (its string, or its list or compound with every element) and makes it a tag.
 * @param value The value; NULL does nothing.
 */
void DataValue_Deconstruct(DataValue* value);


// Functions: questions.
/**
 * @brief Reports whether two values are equal: the same type and the same value, deeply.
 *
 * Floats compare by value (0 equals -0); strings by bytes; keys by id (one registry); lists by element type, count
 * and elements; compounds by their names and values (their order is canonical). An int never equals a float.
 * @param a The first value; must not be NULL.
 * @param b The second value; must not be NULL.
 * @returns true if equal; false otherwise or if an argument is NULL.
 */
bool DataValue_Equals(const DataValue* a, const DataValue* b);

/**
 * @brief Returns a value's nesting depth: 1 for a scalar, an empty list or an empty compound; one more than the
 *        deepest element otherwise. Constant time (containers remember theirs).
 * @param value The value; must not be NULL.
 * @returns The depth; at most DATA_VALUE_MAX_DEPTH for every value the builders made.
 */
size_t DataValue_GetDepth(const DataValue* value);

/**
 * @brief Returns the bytes a value counts against the data limits: an estimate of what it takes saved.
 *
 * A tag 0, a bool 1, an int, float, object or date_time 8, a vector3 12, a color 4, a string its bytes plus 1, a key
 * its text (NAMESPACED_KEY_MAX_LENGTH when @p keys does not know it); a list 1 plus its elements; a compound 1 plus,
 * per entry, its name's bytes, 1 and the value. Every value but a tag counts at least one byte, so a byte limit also
 * bounds how many values there are. A map entry adds its key's text and 1 (see DataMap.h). Walks the value.
 * @param value The value; must not be NULL.
 * @param keys The registry the value's keys come from; may be NULL (every key counts the maximum).
 * @returns The byte count.
 */
size_t DataValue_GetByteSize(const DataValue* value, const KeyRegistry* keys);

/**
 * @brief Checks everything a stored value must be that the builders do not check: floats and vector3 components
 *        finite, keys known to the registry, date_times inside the calendar, every element and entry of a list or
 *        compound valid, and the type known.
 * @param value The value; must not be NULL.
 * @param keys The registry its keys must come from; NULL skips the key check.
 * @returns Success; ErrorCode_IllegalArgument if @p value is NULL; ErrorCode_ArgumentOutOfRange naming the first
 *          problem and where it is (e.g. "[2].speed: the float must be finite").
 */
Error DataValue_Validate(const DataValue* value, const KeyRegistry* keys);


// Functions: lists.
/**
 * @brief Returns a list's element type.
 * @param list The value; must be a list.
 * @returns The element type; DataType_Tag when @p list is NULL or not a list.
 */
DataType DataValue_GetListElementType(const DataValue* list);

/**
 * @brief Returns a list's element count.
 * @param list The value; must be a list.
 * @returns The count; 0 when @p list is NULL or not a list.
 */
size_t DataValue_GetListCount(const DataValue* list);

/**
 * @brief Returns a list's element (owned by the list: valid until the list changes; never change it through a cast).
 * @param list The value; must be a list.
 * @param index The index, below DataValue_GetListCount.
 * @returns The element; NULL when @p list is not a list or @p index is out of range.
 */
const DataValue* DataValue_GetListElement(const DataValue* list, size_t index);

/**
 * @brief Appends an element to a list, MOVING it in: on success the list owns it and @p element becomes a tag.
 * @param list The list; must not be NULL.
 * @param element The element, of the list's element type; must not be NULL. Still the caller's on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL, @p list is not a list or @p element has another
 *          type than the list's elements; ErrorCode_InvalidOperation if the list already holds
 *          DATA_VALUE_MAX_ELEMENT_COUNT elements or the element would nest the list deeper than DATA_VALUE_MAX_DEPTH.
 */
Error DataValue_ListAdd(DataValue* list, DataValue* element);

/**
 * @brief Removes and releases a list's element; the later elements move up.
 * @param list The list; must not be NULL.
 * @param index The index, below DataValue_GetListCount.
 * @returns Success; ErrorCode_IllegalArgument if @p list is NULL or not a list; ErrorCode_IndexOutOfBounds if
 *          @p index is out of range.
 */
Error DataValue_ListRemoveAt(DataValue* list, size_t index);


// Functions: compounds.
/**
 * @brief Returns a compound's entry count.
 * @param compound The value; must be a compound.
 * @returns The count; 0 when @p compound is NULL or not a compound.
 */
size_t DataValue_GetCompoundCount(const DataValue* compound);

/**
 * @brief Returns the name of a compound's entry (entries are sorted by name, byte order).
 * @param compound The value; must be a compound.
 * @param index The index, below DataValue_GetCompoundCount.
 * @returns The name (owned by the compound: valid until it changes); NULL when out of range or not a compound.
 */
const unsigned char* DataValue_GetCompoundEntryName(const DataValue* compound, size_t index);

/**
 * @brief Returns the value of a compound's entry (owned by the compound: valid until it changes).
 * @param compound The value; must be a compound.
 * @param index The index, below DataValue_GetCompoundCount.
 * @returns The value; NULL when out of range or not a compound.
 */
const DataValue* DataValue_GetCompoundEntryValue(const DataValue* compound, size_t index);

/**
 * @brief Finds a compound's entry by name (a binary search).
 * @param compound The value; must be a compound.
 * @param name The NUL-terminated name; NULL finds nothing.
 * @returns The entry's value (owned by the compound: valid until it changes); NULL when absent or not a compound.
 */
const DataValue* DataValue_FindCompoundEntry(const DataValue* compound, const unsigned char* name);

/**
 * @brief Sets a compound's entry, MOVING the value in: on success the compound owns it (an old value under that name
 *        is released) and @p value becomes a tag.
 * @param compound The compound; must not be NULL.
 * @param name The NUL-terminated name (copied): 1 to DATA_VALUE_MAX_NAME_LENGTH bytes of UTF-8, not starting with '$'
 *        except DATA_VALUE_TYPE_ENTRY_NAME, whose value must be a key.
 * @param value The value; must not be NULL. A tag is not a value: it is refused. Still the caller's on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL, @p compound is not a compound or @p value is a
 *          tag; ErrorCode_ArgumentOutOfRange for a bad name (or a "$type" that is not a key);
 *          ErrorCode_InvalidOperation if a new entry would pass DATA_VALUE_MAX_ELEMENT_COUNT or the value would nest
 *          the compound deeper than DATA_VALUE_MAX_DEPTH.
 */
Error DataValue_CompoundSet(DataValue* compound, const unsigned char* name, DataValue* value);

/**
 * @brief Removes and releases a compound's entry.
 * @param compound The compound; must not be NULL.
 * @param name The NUL-terminated name.
 * @returns true if the entry existed.
 */
bool DataValue_CompoundRemove(DataValue* compound, const unsigned char* name);


// Functions: JSON and GHDF (DataValueJSON.c, DataValueGHDF.c).
/**
 * @brief Writes a value as JSON (see the file comment for the forms).
 *
 * Validates the value first (DataValue_Validate), so invalid data is never written. A tag has no JSON form.
 * @param value The value; must not be NULL.
 * @param keys The registry its keys come from (their text is written); must not be NULL.
 * @param pool The pool the JSON value's strings, arrays and compounds are borrowed from; must not be NULL.
 * @param outJSON [out] Receives the JSON value, owned by the caller (store it in a container or return it to
 *        @p pool). Must not be NULL. Nothing stays borrowed on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL or @p value is a tag; the validation error;
 *          ErrorCode_BufferTooLarge if a string could not be built.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error DataValue_WriteJSON(const DataValue* value, const KeyRegistry* keys, JSONObjectPool* pool,
    JSONObjectValue* outJSON);

/**
 * @brief Reads a value from JSON (see the file comment for the forms), interning its keys.
 *
 * Every rule is checked while reading (types, limits, depth, names, keys, UTF-8), so hostile data is refused before
 * anything big is built. Keys may leave out their namespace: @p defaultNamespace is applied. All or nothing: on failure
 * the registry forgets the keys the read interned (KeyRegistry_TruncateTo).
 * @param json The JSON value; must not be NULL.
 * @param keys The registry to intern keys in; must not be NULL.
 * @param defaultNamespace The namespace of keys written without one; NULL means NAMESPACED_KEY_DEFAULT_NAMESPACE.
 * @param outValue [out] Receives the value (release it with DataValue_Deconstruct). Must not be NULL. A tag on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_InvalidJSON naming the problem and
 *          where it is (e.g. "[2].count: expected ...").
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error DataValue_ReadJSON(const JSONObjectValue* json, KeyRegistry* keys, const unsigned char* defaultNamespace,
    DataValue* outValue);

/**
 * @brief Writes a value into a GHDF compound: entry 2 its type, entry 3 its value, entry 4 a list's element type.
 *
 * Validates the value first. Other entries of @p compound are left alone (a map writes its key as entry 1).
 * @param value The value; must not be NULL.
 * @param keys The registry its keys come from; must not be NULL.
 * @param pool The pool every written string, array and compound is borrowed from; must not be NULL.
 * @param compound The compound to write into; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; the validation error; ErrorCode_EncodeError if a
 *          string could not be built. On a failure after validation the compound may hold some entries: discard it.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error DataValue_WriteGHDF(const DataValue* value, const KeyRegistry* keys, GHDFObjectPool* pool,
    GHDFCompound* compound);

/**
 * @brief Reads a value from a GHDF compound written by DataValue_WriteGHDF, interning its keys.
 *
 * Every rule is checked while reading, as for JSON (floats and vector3 components must be finite). Keys must be in
 * the full form. All or nothing, for the registry too, as for JSON.
 * @param compound The compound; must not be NULL.
 * @param keys The registry to intern keys in; must not be NULL.
 * @param outValue [out] Receives the value (release it with DataValue_Deconstruct). Must not be NULL. A tag on failure.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_Deserialize naming the problem and
 *          where it is.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error DataValue_ReadGHDF(GHDFCompound* compound, KeyRegistry* keys, DataValue* outValue);
