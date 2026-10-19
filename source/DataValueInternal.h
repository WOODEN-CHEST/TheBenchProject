#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "DataValue.h"
#include "wr/WRCollection.h"
#include "wr/WRError.h"


/**
 * Private header (source/) of the DataValue module, shared by DataValue.c (values, lists, compounds, validation),
 * DataValueJSON.c and DataValueGHDF.c, and by DataMap.c (which writes and reads its entries the same way). Not public
 * API: the container layouts and the path that names where a problem is in messages.
 */


// Macros.
/** The most segments a path records: a map key and one per nesting level, with room to name the level past the limit. */
#define DATA_PATH_CAPACITY (DATA_VALUE_MAX_DEPTH + 2U)
/** Bytes of stack storage for a collection enumerator; bigger ones are allocated. */
#define DATA_ENUMERATOR_INLINE_SIZE ((size_t)256)
/** The entries of a GHDF value compound: its name in a map or compound (String), its type (UInt8), its value, a
 *  list's element type (UInt8). */
#define DATA_GHDF_NAME ((GHDFEntryID)1)
#define DATA_GHDF_TYPE ((GHDFEntryID)2)
#define DATA_GHDF_VALUE ((GHDFEntryID)3)
#define DATA_GHDF_ELEMENT_TYPE ((GHDFEntryID)4)


// Types.
/** A list: its element type, count, capacity, cached depth, then its elements (one allocation, grown by realloc). */
struct DataListStruct
{
    /** The type of every element; never DataType_Tag. */
    DataType _elementType;
    /** The number of elements. */
    uint32_t _count;
    /** The number of elements _items has room for. */
    uint32_t _capacity;
    /** 1 + the deepest element's depth (1 when empty). */
    uint32_t _depth;
    /** The elements. */
    DataValue _items[];
};

/** One entry of a compound. */
typedef struct DataCompoundEntryStruct
{
    /** Owned, NUL-terminated UTF-8 name. */
    unsigned char* Name;
    /** The value (never a tag). */
    DataValue Value;
} DataCompoundEntry;

/** A compound: its count, capacity, cached depth, then its entries sorted by name (one allocation, grown by realloc). */
struct DataCompoundStruct
{
    /** The number of entries. */
    uint32_t _count;
    /** The number of entries _entries has room for. */
    uint32_t _capacity;
    /** 1 + the deepest value's depth (1 when empty). */
    uint32_t _depth;
    /** The entries, sorted by name (byte order). */
    DataCompoundEntry _entries[];
};

/** One step of a path: a name (a map key or a compound entry) or a list index. */
typedef struct DataPathSegmentStruct
{
    /** The name's bytes (borrowed from the data being read or checked), or NULL for an index. */
    const unsigned char* Name;
    /** The name's byte count. */
    size_t NameLength;
    /** The list index, when Name is NULL. */
    size_t Index;
} DataPathSegment;

/** Where a reader or the validator is inside a value, so messages can say "town:inventory[2].count: ...". Lives on
 *  the stack of the outermost call; pushed and popped as it descends. */
typedef struct DataPathStruct
{
    /** The first DATA_PATH_CAPACITY segments. */
    DataPathSegment Segments[DATA_PATH_CAPACITY];
    /** How many segments are pushed (may pass the capacity: the extra ones are not recorded). */
    size_t Count;
} DataPath;

/** What a reader may still build: the bytes a limit leaves for the value (DataValue_GetByteSize), and the limit, to
 *  name it when the value does not fit. */
typedef struct DataBudgetStruct
{
    /** The bytes the value may still count. */
    size_t Remaining;
    /** The limit those bytes are part of (a DataMapLimits.MaxByteSize). */
    size_t Limit;
} DataBudget;

/** Stack storage for a collection enumerator, aligned for any of them. */
typedef union DataEnumeratorStorageUnion
{
    max_align_t Alignment;
    unsigned char Bytes[DATA_ENUMERATOR_INLINE_SIZE];
} DataEnumeratorStorage;


// Functions: paths.
/** An empty path. */
static inline DataPath DataPath_Create(void)
{
    return (DataPath){ .Count = 0U };
}

/** Pushes a name (a map key or a compound entry name; borrowed until popped). */
void DataPath_PushName(DataPath* self, const unsigned char* name, size_t nameLength);

/** Pushes a list index. */
void DataPath_PushIndex(DataPath* self, size_t index);

/** Pops the last segment. */
static inline void DataPath_Pop(DataPath* self)
{
    self->Count--;
}

/** Makes an error of @p code: "<path>: <problem>.", or "<problem>." at the root. Names are quoted with every byte that
 *  is not printable ASCII as '?', cut to 64 bytes. */
Error DataPath_MakeError(const DataPath* self, ErrorCode code, const char* problem);

/** Makes an error of @p code from @p inner's message the same way, and deconstructs @p inner. */
Error DataPath_WrapError(const DataPath* self, ErrorCode code, Error* inner);


// Functions: values.
/** Reports whether a compound entry name follows the rules: 1 to DATA_VALUE_MAX_NAME_LENGTH bytes of UTF-8 without a
 *  NUL, not starting with '$' unless it is DATA_VALUE_TYPE_ENTRY_NAME. @p bytes need not be NUL-terminated. */
bool DataValueInternal_IsNameValid(const unsigned char* bytes, size_t byteCount);

/** Reports whether @p name (@p byteCount bytes) is DATA_VALUE_TYPE_ENTRY_NAME. */
bool DataValueInternal_IsTypeEntryName(const unsigned char* name, size_t byteCount);

/** Checks a value as DataValue_Validate does, naming problems with @p path (ArgumentOutOfRange). */
Error DataValueInternal_Validate(const DataValue* value, const KeyRegistry* keys, DataPath* path);

/** Begins enumerating a collection in @p storage when its enumerator fits, else in an allocated one; NULL on failure.
 *  End with DataValueInternal_EndEnumeration. */
CollectionEnumerator* DataValueInternal_BeginEnumeration(ICollection* collection, DataEnumeratorStorage* storage);

/** Ends an enumeration begun by DataValueInternal_BeginEnumeration (frees an allocated enumerator). */
void DataValueInternal_EndEnumeration(CollectionEnumerator* enumerator, const DataEnumeratorStorage* storage);


// Functions: the codecs, with a path (DataValueJSON.c, DataValueGHDF.c).
/** Reads a JSON value at nesting level @p level (1 for a value of its own or in a map), naming problems with
 *  @p path (InvalidJSON). @p budget, when not NULL, holds the bytes the value may still count; the reader charges every
 *  part against it as it goes, so a value too large for its limit is refused while it is read, before it is built
 *  whole. @p outValue is a tag on failure. */
Error DataValueInternal_ReadJSON(const JSONObjectValue* json, KeyRegistry* keys, const unsigned char* defaultNamespace,
    size_t level, DataPath* path, DataBudget* budget, DataValue* outValue);

/** Writes a value that was already validated as JSON. */
Error DataValueInternal_WriteJSON(const DataValue* value, const KeyRegistry* keys, JSONObjectPool* pool,
    JSONObjectValue* outJSON);

/** Reads a GHDF value compound (entries 2 to 4) at nesting level @p level, naming problems with @p path (Deserialize),
 *  charging it against @p budget as DataValueInternal_ReadJSON does. @p outValue is a tag on failure. */
Error DataValueInternal_ReadGHDF(GHDFCompound* compound, KeyRegistry* keys, size_t level, DataPath* path,
    DataBudget* budget, DataValue* outValue);

/** Charges @p bytes against a reader's budget (NULL: no budget); when they do not fit, an error of @p failureCode at
 *  @p path that names the limit, as DataMap's own check does ("the data takes at most N bytes"). */
Error DataValueInternal_Charge(DataBudget* budget, size_t bytes, const DataPath* path, ErrorCode failureCode);

/** Validates a value a reader built (DataValueInternal_Validate at @p path), as a last check behind the readers' own:
 *  a problem becomes an error of @p failureCode with the same message. */
Error DataValueInternal_CheckRead(const DataValue* value, const KeyRegistry* keys, DataPath* path, ErrorCode failureCode);

/** Writes a value that was already validated into a GHDF compound (entries 2 to 4). */
Error DataValueInternal_WriteGHDF(const DataValue* value, const KeyRegistry* keys, GHDFObjectPool* pool,
    GHDFCompound* compound);

/** Puts a string entry (a pool string holding @p text and its terminator) into a GHDF compound. */
Error DataValueInternal_PutGHDFString(GHDFObjectPool* pool, GHDFCompound* compound, GHDFEntryID id,
    const unsigned char* text);

/** Reads the name entry (DATA_GHDF_NAME) of a value compound: its bytes without the terminator. Deserialize when it is
 *  absent, not a string, or holds a NUL before its end. */
Error DataValueInternal_GetGHDFName(GHDFCompound* compound, const unsigned char** outBytes, size_t* outByteCount);
