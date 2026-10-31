// World files: the loading rules and the object order both formats share, file reading and atomic saving, and paths
// (WorldFileJSON.c and WorldFileGHDF.c walk each format's tree).

#include <inttypes.h>
#include <stdio.h>
#include "WorldFileInternal.h"
#include "Logger.h"
#include "NamespacedKey.h"
#include "WorldObject.h"
#include "WorldComponent.h"
#include "wr/WRCompile.h"
#include "wr/WRFileStream.h"
#include "wr/WRFileSystem.h"
#include "wr/WRMemory.h"
#include "wr/WRPath.h"
#include "wr/WRString.h"


// Macros.
/** The first format version: it wrote every table at its first version. */
#define WORLD_FILE_FIRST_FORMAT_VERSION ((uint32_t)1)
/** Room for an error prefix such as "object 18446744073709551615" or "objects[99999]". */
#define WORLD_FILE_PREFIX_CAPACITY ((size_t)64)
/** Room for a warning's " (18446744073709551615 times; first in object 18446744073709551615)". */
#define WORLD_FILE_OCCURRENCES_CAPACITY ((size_t)96)
/** The ending of a save's temporary file, next to the target. */
#define WORLD_FILE_TEMPORARY_SUFFIX ((const unsigned char*)u8".tmp")
/** UTF-8 continuation bytes are 10xxxxxx. */
#define WORLD_FILE_UTF8_CONTINUATION_MASK 0xC0U
#define WORLD_FILE_UTF8_CONTINUATION_BITS 0x80U
/** The printable ASCII range a warning shows as it is; other bytes of an invalid name show as '?'. */
#define WORLD_FILE_FIRST_PRINTABLE 0x20U
#define WORLD_FILE_LAST_ASCII 0x7EU
/** DEL, a control character. */
#define WORLD_FILE_DELETE 0x7FU
/** The C1 controls U+0080 to U+009F are C2 80 to C2 9F in UTF-8 (a valid text's C2 is always followed by 80 to BF). */
#define WORLD_FILE_UTF8_C1_LEAD 0xC2U
#define WORLD_FILE_UTF8_C1_LAST_TRAIL 0x9FU


// Static functions: loading.
/* The version of @p table that files of @p formatVersion were written with (PropertyReadOptions.DataVersion). Format 1
 * wrote every table at its first version; formats 2 and 3 (the current one, which writes asset names in full) write
 * every table at its current version (the model, model instances and terrain components' 2, every other table's 1),
 * and the tables' migrations read format 1's data. */
// TODO: when a table's Version rises again (its Migrate converting older data), raise WORLD_FILE_FORMAT_VERSION with it
// and return here the versions formats 2 and 3 wrote (the ones above); tests/WorldFileTests.c checks that every table a
// world file holds is still at the version formats 2 and 3 map it to.
static uint32_t GetTableVersion(uint32_t formatVersion, const PropertyTable* table)
{
    return (formatVersion <= WORLD_FILE_FIRST_FORMAT_VERSION) ? WORLD_FILE_FIRST_FORMAT_VERSION : table->Version;
}

/* Whether @p candidate is @p root or inside its subtree. */
static bool IsInSubtree(const WorldObject* candidate, const WorldObject* root)
{
    for (const WorldObject* Current = candidate; Current != NULL; Current = WorldObject_GetParent(Current))
    {
        if (Current == root)
        {
            return true;
        }
    }
    return false;
}

/* Shows control characters as '?' in place (C1 controls too, which some terminals take as escapes), and every byte
 * above ASCII when the text is not UTF-8, so text from a file cannot break a log line or move the cursor. */
static void MakePrintable(unsigned char* text, size_t length)
{
    bool IsUTF8 = StringUTF8_IsEncodingValid(text) && (StringUTF8_GetByteLength(text) == length);
    for (size_t Index = 0U; Index < length; Index++)
    {
        // U+0080 to U+009F are C2 80 to C2 9F in UTF-8.
        if (IsUTF8 && (text[Index] == WORLD_FILE_UTF8_C1_LEAD) && ((Index + 1U) < length)
            && (text[Index + 1U] <= WORLD_FILE_UTF8_C1_LAST_TRAIL))
        {
            text[Index] = (unsigned char)'?';
            text[Index + 1U] = (unsigned char)'?';
            Index++;
            continue;
        }
        bool IsControl = (text[Index] < WORLD_FILE_FIRST_PRINTABLE) || (text[Index] == WORLD_FILE_DELETE);
        if (IsControl || (!IsUTF8 && (text[Index] > WORLD_FILE_LAST_ASCII)))
        {
            text[Index] = (unsigned char)'?';
        }
    }
}

/* Copies a name into an unknown-name record: at most the print limit, cut on a UTF-8 boundary, made printable
 * (MakePrintable), so a hostile name cannot break a log line. */
static void CopyPrintableName(const unsigned char* name, size_t nameLength, unsigned char* destination)
{
    size_t Length = nameLength;
    if (Length > WORLD_FILE_UNKNOWN_NAME_PRINT_LIMIT)
    {
        Length = WORLD_FILE_UNKNOWN_NAME_PRINT_LIMIT;
        while ((Length > 0U)
            && ((name[Length] & WORLD_FILE_UTF8_CONTINUATION_MASK) == WORLD_FILE_UTF8_CONTINUATION_BITS))
        {
            Length--;
        }
    }
    if (Length > 0U)
    {
        Memory_Copy(name, destination, Length);
    }
    destination[Length] = 0U;
    MakePrintable(destination, Length);
}

static bool IsSameUnknown(const WorldFileUnknownName* entry, WorldFileUnknownKind kind, const unsigned char* where,
    const unsigned char* name)
{
    bool IsSameWhere = (entry->Where == where)
        || ((entry->Where != NULL) && (where != NULL) && (StringUTF8_GetByteLength(entry->Where)
            == StringUTF8_GetByteLength(where)) && Memory_IsEqual(entry->Where, where, StringUTF8_GetByteLength(where)));
    size_t Length = StringUTF8_GetByteLength(name);
    return (entry->Kind == kind) && IsSameWhere && (StringUTF8_GetByteLength(entry->Name) == Length)
        && Memory_IsEqual(entry->Name, name, Length);
}

/* " (3 times; first in object 12)", " (in object 12)" or nothing, for an unknown name's warning. */
static void FormatOccurrences(const WorldFileUnknownName* entry, char* destination, size_t capacity)
{
    destination[0] = '\0';
    if ((entry->Count > 1U) && (entry->FirstObjectId != 0U))
    {
        snprintf(destination, capacity, " (%" PRIu64 " times; first in object %" PRIu64 ")", (uint64_t)entry->Count,
            entry->FirstObjectId);
    }
    else if (entry->Count > 1U)
    {
        snprintf(destination, capacity, " (%" PRIu64 " times)", (uint64_t)entry->Count);
    }
    else if (entry->FirstObjectId != 0U)
    {
        snprintf(destination, capacity, " (in object %" PRIu64 ")", entry->FirstObjectId);
    }
}

/* One warning per unknown name the load skipped, then one for those past the limit. */
static void LogUnknownNames(const WorldFileLoad* self)
{
    Logger* Log = self->Options->Logger;
    if (Log == NULL)
    {
        return;
    }
    const WorldFileUnknownName* Entries = (const WorldFileUnknownName*)self->UnknownNames._data;
    for (size_t Index = 0U; Index < self->UnknownNames._count; Index++)
    {
        const WorldFileUnknownName* Entry = &Entries[Index];
        char Occurrences[WORLD_FILE_OCCURRENCES_CAPACITY];
        FormatOccurrences(Entry, Occurrences, sizeof(Occurrences));
        const char* Where = (Entry->Where != NULL) ? (const char*)Entry->Where : "?";
        Error Result = Error_CreateSuccess();
        switch (Entry->Kind)
        {
            case WorldFileUnknownKind_Key:
                Result = Logger_LogWarningFormatted(Log, u8"World file: skipped the unknown key \"%s\" in %s%s.",
                    (const char*)Entry->Name, Where, Occurrences);
                break;
            case WorldFileUnknownKind_Entry:
                Result = Logger_LogWarningFormatted(Log, u8"World file: skipped the unknown entry %s in %s%s.",
                    (const char*)Entry->Name, Where, Occurrences);
                break;
            case WorldFileUnknownKind_Component:
                Result = Logger_LogWarningFormatted(Log, u8"World file: skipped the unknown component \"%s\"%s.",
                    (const char*)Entry->Name, Occurrences);
                break;
            case WorldFileUnknownKind_ComponentType:
                Result = Logger_LogWarningFormatted(Log, u8"World file: skipped the unknown component type %s%s.",
                    (const char*)Entry->Name, Occurrences);
                break;
        }
        Error_Deconstruct(&Result);
    }
    if (self->UnlistedUnknownCount > 0U)
    {
        Error Result = Logger_LogWarningFormatted(Log, u8"World file: skipped %" PRIu64 " more unknown names.",
            (uint64_t)self->UnlistedUnknownCount);
        Error_Deconstruct(&Result);
    }
}


// Internal functions: loading.
Error WorldFileLoad_Begin(WorldFileLoad* self, const WorldFileReadOptions* options, World* outWorld,
    ErrorCode formatErrorCode)
{
    if ((self == NULL) || (options == NULL) || (options->Keys == NULL) || (outWorld == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "World file: the options, their key registry and the world must not be NULL.");
    }
    Memory_Zero(self, sizeof(*self));
    self->Options = options;
    self->Target = outWorld;
    self->FormatErrorCode = formatErrorCode;
    self->KeyCheckpoint = KeyRegistry_GetCount(options->Keys);
    self->FormatVersion = WORLD_FILE_FIRST_FORMAT_VERSION;
    Error Result = World_Construct2(outWorld, options->Keys, options->Assets, options->Logger);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->IsTargetConstructed = true;
    GenericBuffer_AllocateVariable(&self->UnknownNames, 0U, sizeof(WorldFileUnknownName));
    return Error_CreateSuccess();
}

Error WorldFileLoad_SetFormatVersion(WorldFileLoad* self, uint64_t version)
{
    if (version == 0U)
    {
        return Error_Construct2(self->FormatErrorCode, "The format version must be at least 1.");
    }
    if (version > (uint64_t)WORLD_FILE_FORMAT_VERSION)
    {
        return Error_Construct4(self->FormatErrorCode,
            "The world file's format version %" PRIu64 " is newer than this game reads (%" PRIu64 ").", version,
            (uint64_t)WORLD_FILE_FORMAT_VERSION);
    }
    self->FormatVersion = (uint32_t)version;
    return Error_CreateSuccess();
}

PropertyReadOptions WorldFileLoad_GetTableOptions(const WorldFileLoad* self, const PropertyTable* table)
{
    PropertyReadOptions Options = PropertyReadOptions_CreateDefault();
    Options.UnknownKeyMode = PropertyUnknownKeyMode_Ignore;
    Options.Logger = self->Options->Logger;
    Options.DataVersion = GetTableVersion(self->FormatVersion, table);
    Options.Pool = self->JSONPool;
    Options.GHDFPool = self->GHDFPool;
    Options.Keys = self->Options->Keys;
    Options.DefaultNamespace = self->Options->DefaultNamespace;
    return Options;
}

Error WorldFileLoad_SetName(WorldFileLoad* self, const unsigned char* text, size_t byteCount)
{
    if (byteCount > WORLD_NAME_MAX_LENGTH)
    {
        return Error_Construct4(self->FormatErrorCode, "The world's name is longer than the %" PRIu64 " bytes allowed.",
            (uint64_t)WORLD_NAME_MAX_LENGTH);
    }
    unsigned char Name[WORLD_NAME_MAX_LENGTH + 1U];
    if (byteCount > 0U)
    {
        // An empty JSON string has no storage (NULL), and memcpy must not be given NULL even for no bytes.
        Memory_Copy(text, Name, byteCount);
    }
    Name[byteCount] = 0U;
    if (StringUTF8_GetByteLength(Name) != byteCount)
    {
        return Error_Construct2(self->FormatErrorCode, "The world's name must not contain NUL characters.");
    }
    Error Result = World_SetName(self->Target, Name);
    return (Result.Code == ErrorCode_Success) ? Result : WorldFileLoad_Wrap(self, "name", &Result);
}

Error WorldFileLoad_CheckObjectCount(const WorldFileLoad* self, size_t count)
{
    if (count > WORLD_FILE_MAX_OBJECT_COUNT)
    {
        return Error_Construct4(self->FormatErrorCode,
            "The file holds %" PRIu64 " objects; a world file holds at most %" PRIu64 ".", (uint64_t)count,
            (uint64_t)WORLD_FILE_MAX_OBJECT_COUNT);
    }
    return Error_CreateSuccess();
}

Error WorldFileLoad_CreateObject(WorldFileLoad* self, size_t index, uint64_t id, WorldObject** outObject)
{
    WorldObject* Existing = NULL;
    if (id == 0U)
    {
        return Error_Construct4(self->FormatErrorCode, "objects[%" PRIu64 "]: the id must not be 0.", (uint64_t)index);
    }
    if (id > WORLD_OBJECT_ID_MAX)
    {
        // A world has no next id past it (World_CreateObjectWithId refuses it too).
        return Error_Construct4(self->FormatErrorCode, "objects[%" PRIu64 "]: the id must be at most %" PRIu64 ".",
            (uint64_t)index, WORLD_OBJECT_ID_MAX);
    }
    if (World_TryGetObject(self->Target, id, &Existing))
    {
        return Error_Construct4(self->FormatErrorCode, "objects[%" PRIu64 "]: the id %" PRIu64 " is used twice.",
            (uint64_t)index, id);
    }
    Error Result = World_CreateObjectWithId(self->Target, NULL, id, outObject);
    if (Result.Code != ErrorCode_Success)
    {
        char Prefix[WORLD_FILE_PREFIX_CAPACITY];
        snprintf(Prefix, sizeof(Prefix), "objects[%" PRIu64 "]", (uint64_t)index);
        return WorldFileLoad_Wrap(self, Prefix, &Result);
    }
    return Error_CreateSuccess();
}

Error WorldFileLoad_CheckParent(const WorldFileLoad* self, const WorldObject* object, uint64_t refusedBefore,
    uint64_t requestedParentId)
{
    if (World_GetRefusedParentCount(self->Target) == refusedBefore)
    {
        return Error_CreateSuccess();
    }
    WorldObject* Parent = NULL;
    const char* Reason = "the hierarchy would be deeper than 64 levels";
    if (!World_TryGetObject(self->Target, requestedParentId, &Parent))
    {
        Reason = "no object has that id";
    }
    else if (Parent == object)
    {
        Reason = "an object cannot be its own parent";
    }
    else if (IsInSubtree(Parent, object))
    {
        Reason = "it is inside the object's own subtree (a cycle)";
    }
    return Error_Construct4(self->FormatErrorCode, "object %" PRIu64 ": its parent %" PRIu64 " was refused: %s.",
        WorldObject_GetId(object), requestedParentId, Reason);
}

Error WorldFileLoad_AddComponent(WorldFileLoad* self, WorldObject* object, WorldComponentType type,
    WorldComponent** outComponent)
{
    Error Result = World_AddComponent(self->Target, WorldObject_GetId(object), type, outComponent);
    if (Result.Code == ErrorCode_Success)
    {
        return Result;
    }
    const WorldComponentTypeInfo* Info = World_GetComponentTypeInfo(self->Target, type);
    char Prefix[WORLD_FILE_PREFIX_CAPACITY * 2U];
    snprintf(Prefix, sizeof(Prefix), "object %" PRIu64 ": component \"%s\"", WorldObject_GetId(object),
        ((Info != NULL) && (Info->Name != NULL)) ? (const char*)Info->Name : "?");
    return WorldFileLoad_Wrap(self, Prefix, &Result);
}

Error WorldFileLoad_CheckDataLimit(const WorldFileLoad* self, const WorldObject* object)
{
    if (World_GetDataByteSize(self->Target) > WORLD_DATA_MAX_BYTES)
    {
        return Error_Construct4(self->FormatErrorCode,
            "object %" PRIu64 ": the objects' tags and data pass the %" PRIu64 " bytes a world may hold.",
            WorldObject_GetId(object), (uint64_t)WORLD_DATA_MAX_BYTES);
    }
    return Error_CreateSuccess();
}

void WorldFileLoad_NoteUnknown(WorldFileLoad* self, WorldFileUnknownKind kind, const unsigned char* where,
    const unsigned char* name, size_t nameLength, uint64_t objectId)
{
    WorldFileUnknownName Candidate;
    Memory_Zero(&Candidate, sizeof(Candidate));
    Candidate.Kind = kind;
    Candidate.Where = where;
    CopyPrintableName(name, nameLength, Candidate.Name);
    Candidate.Count = 1U;
    Candidate.FirstObjectId = objectId;

    WorldFileUnknownName* Entries = (WorldFileUnknownName*)self->UnknownNames._data;
    for (size_t Index = 0U; Index < self->UnknownNames._count; Index++)
    {
        if (IsSameUnknown(&Entries[Index], kind, where, Candidate.Name))
        {
            Entries[Index].Count++;
            return;
        }
    }
    if ((self->UnknownNames._count >= WORLD_FILE_UNKNOWN_NAME_LIMIT)
        || !GenericBuffer_AddLast(&self->UnknownNames, &Candidate))
    {
        self->UnlistedUnknownCount++;
    }
}

Error WorldFileLoad_Finish(WorldFileLoad* self, bool hasNextObjectId, uint64_t nextObjectId)
{
    // Creating the objects moved the counter past every id, so a file's next id below it would hand an id out twice.
    uint64_t Lowest = World_GetNextObjectId(self->Target);
    bool IsRaised = hasNextObjectId && (nextObjectId < Lowest);
    if (hasNextObjectId && !IsRaised)
    {
        World_EnsureNextObjectIdAtLeast(self->Target, nextObjectId);
    }
    World_UpdateTransforms(self->Target);

    LogUnknownNames(self);
    if (IsRaised && (self->Options->Logger != NULL))
    {
        Error LogResult = Logger_LogWarningFormatted(self->Options->Logger,
            u8"World file: the next object id %" PRIu64 " is not above every object id; raised to %" PRIu64 ".",
            nextObjectId, Lowest);
        Error_Deconstruct(&LogResult);
    }
    Memory_Free(self->UnknownNames._data);
    self->UnknownNames._data = NULL;
    return Error_CreateSuccess();
}

Error WorldFileLoad_Fail(WorldFileLoad* self, Error error)
{
    if (self->IsTargetConstructed)
    {
        Error DeconstructResult = World_Deconstruct(self->Target);
        Error_Deconstruct(&DeconstructResult);
        self->IsTargetConstructed = false;
    }
    // Nothing holds a key id the load interned any more: the world that held them is gone.
    KeyRegistry_TruncateTo(self->Options->Keys, self->KeyCheckpoint);
    Memory_Free(self->UnknownNames._data);
    self->UnknownNames._data = NULL;
    return error;
}

Error WorldFileLoad_Wrap(const WorldFileLoad* self, const char* prefix, Error* inner)
{
    if (inner->Message == NULL)
    {
        Error_Deconstruct(inner);
        return Error_Construct4(self->FormatErrorCode, "%s: invalid value.", prefix);
    }
    // The inner message may quote the file (a key, a name), so it is made printable before it reaches a log.
    size_t Length = StringUTF8_GetByteLength(inner->Message);
    unsigned char* Printable = Memory_Allocate(Length + 1U);
    Memory_Copy(inner->Message, Printable, Length + 1U);
    MakePrintable(Printable, Length);
    Error Result = Error_Construct4(self->FormatErrorCode, "%s: %s", prefix, (const char*)Printable);
    Memory_Free(Printable);
    Error_Deconstruct(inner);
    return Result;
}

Error WorldFileLoad_WrapObject(const WorldFileLoad* self, uint64_t objectId, Error* inner)
{
    char Prefix[WORLD_FILE_PREFIX_CAPACITY];
    snprintf(Prefix, sizeof(Prefix), "object %" PRIu64, objectId);
    return WorldFileLoad_Wrap(self, Prefix, inner);
}


// Internal functions: enumeration.
CollectionEnumerator* WorldFileInternal_BeginEnumeration(ICollection* collection, WorldFileEnumeratorStorage* storage)
{
    if (ICollection_GetEnumeratorSize(collection) <= sizeof(storage->Bytes))
    {
        return ICollection_InitEnumerator(collection, storage->Bytes);
    }
    return ICollection_CreateEnumerator(collection);
}

void WorldFileInternal_EndEnumeration(CollectionEnumerator* enumerator, const WorldFileEnumeratorStorage* storage)
{
    if (enumerator == NULL)
    {
        return;
    }
    if ((const void*)enumerator == (const void*)storage->Bytes)
    {
        CollectionEnumerator_Deconstruct(enumerator);
        return;
    }
    CollectionEnumerator_Destroy(enumerator);
}


// Static functions: writing.
static ComparisonResult CompareObjectsById(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    UNUSED(buffer);
    UNUSED(userData);
    uint64_t First = WorldObject_GetId(*(WorldObject* const*)a._element);
    uint64_t Second = WorldObject_GetId(*(WorldObject* const*)b._element);
    if (First == Second)
    {
        return ComparisonResult_AEqualsB;
    }
    return (First < Second) ? ComparisonResult_ALessThanB : ComparisonResult_AGreaterThanB;
}

/* @p object or the first of its later siblings that files save (WorldObject_IsPersistent), or NULL. */
static WorldObject* SkipRuntimeObjects(WorldObject* object)
{
    while ((object != NULL) && !WorldObject_IsPersistent(object))
    {
        object = WorldObject_GetNextSibling(object);
    }
    return object;
}

/* Appends @p root (saved) and its subtree depth first, children in attach order, leaving out runtime objects with their
 * subtrees (no recursion: the links lead back up). */
static bool AppendSubtree(WorldObject* root, GenericBuffer* outObjects)
{
    WorldObject* Current = root;
    while (Current != NULL)
    {
        if (!GenericBuffer_AddLast(outObjects, &Current))
        {
            return false;
        }
        WorldObject* Child = SkipRuntimeObjects(WorldObject_GetFirstChild(Current));
        if (Child != NULL)
        {
            Current = Child;
            continue;
        }
        while ((Current != root) && (SkipRuntimeObjects(WorldObject_GetNextSibling(Current)) == NULL))
        {
            Current = WorldObject_GetParent(Current);
        }
        Current = (Current == root) ? NULL : SkipRuntimeObjects(WorldObject_GetNextSibling(Current));
    }
    return true;
}


// Internal functions: writing.
Error WorldFileInternal_CollectObjects(const World* world, GenericBuffer* outObjects)
{
    size_t Count = World_GetObjectCount(world);
    GenericBuffer Roots;
    GenericBuffer_AllocateVariable(&Roots, Count, sizeof(WorldObject*));
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < Count) && (Result.Code == ErrorCode_Success); Index++)
    {
        WorldObject* Object = NULL;
        Result = World_GetObjectByIndex(world, Index, &Object);
        if ((Result.Code == ErrorCode_Success) && (WorldObject_GetParent(Object) == NULL)
            && WorldObject_IsPersistent(Object) && !GenericBuffer_AddLast(&Roots, &Object))
        {
            Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not list the world's objects.");
        }
    }
    if ((Result.Code == ErrorCode_Success) && (Roots._count > 1U))
    {
        WorldObject* Scratch[2];
        GenericBuffer_SortAscending(&Roots, CompareObjectsById, NULL, Scratch);
    }
    WorldObject* const* RootObjects = (WorldObject* const*)Roots._data;
    for (size_t Index = 0U; (Index < Roots._count) && (Result.Code == ErrorCode_Success); Index++)
    {
        if (!AppendSubtree(RootObjects[Index], outObjects))
        {
            Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not list the world's objects.");
        }
    }
    Memory_Free(Roots._data);
    return Result;
}

uint64_t WorldFileInternal_GetNextObjectIdToWrite(const World* world)
{
    uint64_t Next = World_GetNextObjectId(world);
    return (Next == 0U) ? UINT64_MAX : Next;
}

Error WorldFileInternal_WrapObjectError(uint64_t objectId, Error* inner)
{
    Error Result = Error_Construct4(inner->Code, "object %" PRIu64 ": %s", objectId,
        (inner->Message != NULL) ? (const char*)inner->Message : "invalid value.");
    Error_Deconstruct(inner);
    return Result;
}


// Static functions: files.
/* Appends the file's bytes to @p outBytes, refusing a file longer than WORLD_FILE_MAX_BYTE_COUNT before reading it. */
static Error ReadFileBytes(const unsigned char* path, ErrorCode tooLongCode, GenericBuffer* outBytes)
{
    FileStream Stream;
    Error Result = FileSystem_OpenFileStream(path, FileOpenMode_ReadBinary, &Stream);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Size = 0U;
    Result = IOStream_GetStreamSizeTotal(FileStream_AsIOStream(&Stream), &Size);
    if ((Result.Code == ErrorCode_Success) && (Size > WORLD_FILE_MAX_BYTE_COUNT))
    {
        Result = Error_Construct4(tooLongCode, "The file has %" PRIu64 " bytes; a world file has at most %" PRIu64 ".",
            (uint64_t)Size, (uint64_t)WORLD_FILE_MAX_BYTE_COUNT);
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_TryPrepareForManualMutation(outBytes, Size))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "Could not hold the world file.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = IOStream_ReadAll(FileStream_AsIOStream(&Stream), outBytes);
    }
    Error CloseResult = FileStream_Deconstruct(&Stream);
    if (Result.Code == ErrorCode_Success)
    {
        return CloseResult;
    }
    Error_Deconstruct(&CloseResult);
    return Result;
}

/* Writes @p bytes to "<path>.tmp" next to the target, then replaces the target with it (FileSystem_ReplaceEntry), so
 * the file at @p path is the old one or the new one, never half written. Missing parent directories are created. */
static Error SaveBytesAtomically(const unsigned char* path, const GenericBuffer* bytes)
{
    if (path == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World file: the path must not be NULL.");
    }
    GenericBuffer Parent;
    GenericBuffer_AllocateVariable(&Parent, 0U, sizeof(unsigned char));
    Error Result = Path_GetParentPath(path, &Parent);
    if ((Result.Code == ErrorCode_Success) && (Parent._count > 0U) && (((const unsigned char*)Parent._data)[0] != 0U))
    {
        Result = FileSystem_CreateAllDirectories(Parent._data);
    }
    Memory_Free(Parent._data);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    GenericBuffer Temporary;
    GenericBuffer_AllocateVariable(&Temporary, 0U, sizeof(unsigned char));
    if (!GenericBuffer_AppendString(&Temporary, path) || !GenericBuffer_AppendString(&Temporary,
        WORLD_FILE_TEMPORARY_SUFFIX) || !GenericBuffer_NullTerminate(&Temporary))
    {
        Memory_Free(Temporary._data);
        return Error_Construct2(ErrorCode_BufferTooSmall, "World file: could not build the temporary path.");
    }
    Result = FileSystem_WriteAllBytes(Temporary._data, bytes->_data, bytes->_count);
    if (Result.Code == ErrorCode_Success)
    {
        Result = FileSystem_ReplaceEntry(Temporary._data, path);
    }
    if (Result.Code != ErrorCode_Success)
    {
        // Best effort: the target is unchanged either way.
        Error DeleteResult = FileSystem_DeleteEntry(Temporary._data);
        Error_Deconstruct(&DeleteResult);
    }
    Memory_Free(Temporary._data);
    return Result;
}

typedef Error (*WorldFileReadFunction)(const unsigned char* bytes, size_t byteCount, const WorldFileReadOptions* options,
    World* outWorld);

typedef Error (*WorldFileWriteFunction)(const World* world, GenericBuffer* outBytes);

static Error LoadFile(const unsigned char* path, const WorldFileReadOptions* options, World* outWorld,
    ErrorCode tooLongCode, WorldFileReadFunction read)
{
    if ((path == NULL) || (options == NULL) || (outWorld == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "World file: the path, options and world must not be NULL.");
    }
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    Error Result = ReadFileBytes(path, tooLongCode, &Bytes);
    if (Result.Code == ErrorCode_Success)
    {
        Result = read(Bytes._data, Bytes._count, options, outWorld);
    }
    Memory_Free(Bytes._data);
    return Result;
}

static Error SaveFile(const World* world, const unsigned char* path, WorldFileWriteFunction write)
{
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    Error Result = write(world, &Bytes);
    if (Result.Code == ErrorCode_Success)
    {
        Result = SaveBytesAtomically(path, &Bytes);
    }
    Memory_Free(Bytes._data);
    return Result;
}


// Public functions.
Error WorldFile_BuildPath(const unsigned char* baseDirectory, const unsigned char* name, WorldFileFormat format,
    GenericBuffer* outPath)
{
    if ((baseDirectory == NULL) || (name == NULL) || (name[0] == 0U) || (outPath == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldFile_BuildPath: the base directory, a non-empty name and the path buffer are required.");
    }
    // TODO: story packs give each pack its own worlds folder and name worlds by pack; until they exist every world
    // lives in <base>/worlds.
    const unsigned char* Extension = WORLD_FILE_JSON_EXTENSION;
    switch (format)
    {
        case WorldFileFormat_JSON:
            Extension = WORLD_FILE_JSON_EXTENSION;
            break;
        case WorldFileFormat_GHDF:
            Extension = WORLD_FILE_GHDF_EXTENSION;
            break;
    }
    GenericBuffer FileName;
    GenericBuffer_AllocateVariable(&FileName, 0U, sizeof(unsigned char));
    Error Result = Error_CreateSuccess();
    if (!GenericBuffer_AppendString(&FileName, name) || !GenericBuffer_AppendString(&FileName, Extension)
        || !GenericBuffer_NullTerminate(&FileName))
    {
        Result = Error_Construct2(ErrorCode_BufferTooSmall, "WorldFile_BuildPath: could not build the file name.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        const unsigned char* Parts[3] = { baseDirectory, WORLD_FILE_DIRECTORY, FileName._data };
        Result = Path_Combine(Parts, 3U, outPath);
    }
    Memory_Free(FileName._data);
    return Result;
}

Error WorldFile_LoadJSON(const unsigned char* path, const WorldFileReadOptions* options, World* outWorld)
{
    return LoadFile(path, options, outWorld, ErrorCode_InvalidJSON, WorldFile_ReadJSON);
}

Error WorldFile_LoadGHDF(const unsigned char* path, const WorldFileReadOptions* options, World* outWorld)
{
    return LoadFile(path, options, outWorld, ErrorCode_Deserialize, WorldFile_ReadGHDF);
}

Error WorldFile_SaveJSON(const World* world, const unsigned char* path)
{
    return SaveFile(world, path, WorldFile_WriteJSON);
}

Error WorldFile_SaveGHDF(const World* world, const unsigned char* path)
{
    return SaveFile(world, path, WorldFile_WriteGHDF);
}
