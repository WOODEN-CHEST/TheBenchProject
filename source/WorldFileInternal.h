#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WorldFile.h"
#include "PropertyDescriptor.h"
#include "wr/WRCollection.h"


/**
 * Private to the world file sources (WorldFile.c, WorldFileJSON.c, WorldFileGHDF.c). The loading rules and the object
 * order are written once here; each format walks its own tree and calls them, so JSON and GHDF load by the same rules.
 *
 * A load: WorldFileLoad_Begin (checks the options, constructs the world in the caller's memory), WorldFileLoad_Set*
 * for the root values, WorldFileLoad_CheckObjectCount, WorldFileLoad_CreateObject for every object (all of them before
 * any table is read, so each parent names an object that exists), then per object its table read, WorldFileLoad_
 * CheckParent, its components through WorldFileLoad_AddComponent and WorldFileLoad_CheckDataLimit; then
 * WorldFileLoad_Finish on success or WorldFileLoad_Fail on any error. Both end the load.
 */


// Macros.
/** How many distinct unknown names a load reports by name (one warning each); the rest are counted. */
#define WORLD_FILE_UNKNOWN_NAME_LIMIT ((size_t)32)
/** How many bytes of an unknown name its warning shows (cut on a UTF-8 boundary). */
#define WORLD_FILE_UNKNOWN_NAME_PRINT_LIMIT ((size_t)64)
/** Room for a collection enumerator on the stack, so walking a compound allocates nothing. */
#define WORLD_FILE_ENUMERATOR_STORAGE_SIZE ((size_t)256)


// Types.
/** What an unknown name is, for its warning. */
typedef enum WorldFileUnknownKindEnum
{
    /** A JSON key no table or block knows. */
    WorldFileUnknownKind_Key,
    /** A GHDF entry id no table or block knows. */
    WorldFileUnknownKind_Entry,
    /** A component type name (JSON) the world does not have. */
    WorldFileUnknownKind_Component,
    /** A component type id (GHDF) the world does not have. */
    WorldFileUnknownKind_ComponentType
} WorldFileUnknownKind;

/** One unknown name a load skipped, with how often and where it was first met. */
typedef struct WorldFileUnknownNameStruct
{
    WorldFileUnknownKind Kind;
    /** Static text: the table or block it was in ("world_object", "the root"); unused for components. */
    const unsigned char* Where;
    /** The name (a key, or an id's decimal text), NUL-terminated, cut to WORLD_FILE_UNKNOWN_NAME_PRINT_LIMIT bytes. */
    unsigned char Name[WORLD_FILE_UNKNOWN_NAME_PRINT_LIMIT + 1U];
    size_t Count;
    /** The first object it was met in; 0 outside objects. */
    uint64_t FirstObjectId;
} WorldFileUnknownName;

/** Stack storage for a collection enumerator (WorldFileInternal_BeginEnumeration). */
typedef union WorldFileEnumeratorStorageUnion
{
    max_align_t Alignment;
    unsigned char Bytes[WORLD_FILE_ENUMERATOR_STORAGE_SIZE];
} WorldFileEnumeratorStorage;

/** The state of one load: the world being built, the key registry's checkpoint and the warnings to give. */
typedef struct WorldFileLoadStruct
{
    const WorldFileReadOptions* Options;
    /** The world being built (the caller's memory); constructed once IsTargetConstructed is true. */
    World* Target;
    bool IsTargetConstructed;
    /** The format's error code: ErrorCode_InvalidJSON or ErrorCode_Deserialize. */
    ErrorCode FormatErrorCode;
    /** The registry's key count before the load, so a failed load gives back what it interned. */
    size_t KeyCheckpoint;
    /** The file's format version (1 to WORLD_FILE_FORMAT_VERSION) once read. */
    uint32_t FormatVersion;
    /** WorldFileUnknownName entries, at most WORLD_FILE_UNKNOWN_NAME_LIMIT; owned. */
    GenericBuffer UnknownNames;
    /** Unknown names met beyond the limit (each time counted). */
    size_t UnlistedUnknownCount;
} WorldFileLoad;


// Functions: loading (WorldFile.c).
/** Checks the options and the output, takes the key checkpoint and constructs the world in @p outWorld. On failure
 *  nothing is constructed. Pair with WorldFileLoad_Finish or WorldFileLoad_Fail. */
Error WorldFileLoad_Begin(WorldFileLoad* self, const WorldFileReadOptions* options, World* outWorld,
    ErrorCode formatErrorCode);

/** The file's format version: refuses 0 and versions newer than WORLD_FILE_FORMAT_VERSION. */
Error WorldFileLoad_SetFormatVersion(WorldFileLoad* self, uint64_t version);

/** The read options of a table's block: the registry, the namespace, the data version the file's format wrote the
 *  table with, and unknown keys ignored (the load reports them once per name itself). */
PropertyReadOptions WorldFileLoad_GetTableOptions(const WorldFileLoad* self, const PropertyTable* table);

/** The world's name from the file (@p byteCount bytes, no NUL inside; World_SetName's rules). */
Error WorldFileLoad_SetName(WorldFileLoad* self, const unsigned char* text, size_t byteCount);

/** Refuses more than WORLD_FILE_MAX_OBJECT_COUNT objects. */
Error WorldFileLoad_CheckObjectCount(const WorldFileLoad* self, size_t count);

/** Creates the object @p index of the file's list with its id: refuses 0, ids above WORLD_OBJECT_ID_MAX and ids used
 *  before. */
Error WorldFileLoad_CreateObject(WorldFileLoad* self, size_t index, uint64_t id, WorldObject** outObject);

/** After an object's table was read: when the world refused the parent it brought (the refused-parent count passed
 *  @p refusedBefore), the error names the object, the parent (@p requestedParentId, what the file asked for) and why. */
Error WorldFileLoad_CheckParent(const WorldFileLoad* self, const WorldObject* object, uint64_t refusedBefore,
    uint64_t requestedParentId);

/** Adds a component of a known type to an object (its default values; the caller reads its block). */
Error WorldFileLoad_AddComponent(WorldFileLoad* self, WorldObject* object, WorldComponentType type,
    WorldComponent** outComponent);

/** Refuses a world whose objects' data passed WORLD_DATA_MAX_BYTES (checked after each object, so a hostile file stops
 *  early). */
Error WorldFileLoad_CheckDataLimit(const WorldFileLoad* self, const WorldObject* object);

/** Notes an unknown name the load skipped; one warning per distinct name is given when the load succeeds. */
void WorldFileLoad_NoteUnknown(WorldFileLoad* self, WorldFileUnknownKind kind, const unsigned char* where,
    const unsigned char* name, size_t nameLength, uint64_t objectId);

/** Ends a successful load: the next object id (raised with a warning when the file's is not above every id), the world
 *  matrices, the warnings. On failure it fails the load (WorldFileLoad_Fail) and returns that error. */
Error WorldFileLoad_Finish(WorldFileLoad* self, bool hasNextObjectId, uint64_t nextObjectId);

/** Ends a failed load: destroys what was built, gives back the keys it interned, and returns @p error. */
Error WorldFileLoad_Fail(WorldFileLoad* self, Error error);

/** "<prefix>: <inner message>" with the load's error code; deconstructs @p inner. */
Error WorldFileLoad_Wrap(const WorldFileLoad* self, const char* prefix, Error* inner);

/** "object <id>: <inner message>" with the load's error code; deconstructs @p inner. */
Error WorldFileLoad_WrapObject(const WorldFileLoad* self, uint64_t objectId, Error* inner);


// Functions: enumeration (WorldFile.c).
/** An enumerator over @p collection, in @p storage when it fits there (no allocation), else allocated; NULL when it
 *  could not be made. End it with WorldFileInternal_EndEnumeration. */
CollectionEnumerator* WorldFileInternal_BeginEnumeration(ICollection* collection, WorldFileEnumeratorStorage* storage);

/** Ends an enumerator from WorldFileInternal_BeginEnumeration; NULL is ignored. */
void WorldFileInternal_EndEnumeration(CollectionEnumerator* enumerator, const WorldFileEnumeratorStorage* storage);


// Functions: writing (WorldFile.c).
/** Appends the world's objects (WorldObject*) in file order: the roots by id, each followed by its subtree depth first,
 *  children in attach order. Runtime objects (WorldObject_IsPersistent false) are left out with their subtrees. */
Error WorldFileInternal_CollectObjects(const World* world, GenericBuffer* outObjects);

/** The next object id a file records: the world's counter, or UINT64_MAX once every id was handed out (the counter is
 *  0 then; no id is UINT64_MAX, so a reader's world has every id used too, World_EnsureNextObjectIdAtLeast). */
uint64_t WorldFileInternal_GetNextObjectIdToWrite(const World* world);

/** "object <id>: <inner message>", keeping the inner error's code (the writers' errors); deconstructs @p inner. */
Error WorldFileInternal_WrapObjectError(uint64_t objectId, Error* inner);
