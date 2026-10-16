#include <inttypes.h>
#include "AssetManager.h"
#include "AssetTypesStandard.h"
#include "Logger.h"
#include "wr/WRHashMap.h"
#include "wr/WRHashSet.h"
#include "wr/WRArrayList.h"
#include "wr/WRList.h"
#include "wr/WRMap.h"
#include "wr/WRSet.h"
#include "wr/WRCollection.h"
#include "wr/WRHash.h"
#include "wr/WRBufferPool.h"
#include "wr/WRFileSystem.h"
#include "wr/WRPath.h"
#include "wr/WRFileStream.h"
#include "wr/WRMemoryStream.h"
#include "wr/WRString.h"
#include "wr/WRNumber.h"
#include "wr/WRJSON.h"


// Macros.
/** Default cache directory (relative to the working directory) for materialized temp files. */
#define ASSET_MANAGER_DEFAULT_CACHE_DIRECTORY ((const unsigned char*)u8".wr_asset_cache")
/** Read stamp of a definition registered in code rather than read from a file. */
#define DEFINITION_READ_STAMP_CODE ((uint64_t)0)
/** Separator between the entries of a name list in an error message. */
#define NAME_LIST_SEPARATOR ((const unsigned char*)u8", ")
/** Shown in messages for an Error that carries no message. */
#define ERROR_MESSAGE_FALLBACK ((const unsigned char*)u8"no details")
/** Listed instead of a file name that is not valid UTF-8 (it cannot be copied into a message). */
#define INVALID_FILE_NAME_PLACEHOLDER ((const unsigned char*)u8"(a file whose name is not valid UTF-8)")
/** Listed by AssetExtensionFilter_AppendList for a filter that accepts everything. */
#define ANY_EXTENSION_TEXT ((const unsigned char*)u8"any extension")


// Types.
/* Composite key for the definition and loaded-asset maps: (type id, borrowed name pointer). The Name
   pointer aliases an externally-owned, stable string (a definition's Name, or a loaded record's Name) that
   outlives the map entry. Keys are stored by value; the comparator does a string compare, not a pointer
   compare, so a lookup key may borrow any matching string. */
typedef struct AssetKeyStruct
{
    AssetTypeID Type;
    const unsigned char* Name;
} AssetKey;

/* A registered asset type. */
typedef struct TypeRecordStruct
{
    AssetTypeID ID;
    unsigned char* Name;                    // owned
    unsigned char* DirectoryName;           // owned
    unsigned char* DefinitionFileExtension; // owned, may be NULL (no filter)
    AssetDefinitionConstructor Constructor;
    UserData ConstructorUserData;
} TypeRecord;

/* A registered definition plus what the manager knows about it. Stored BY VALUE in _definitions, so a
   pointer to one (TryGetDefinitionRecord) is only valid until the next insertion into or removal from that
   map; never hold one across such a change (an asset load may change it through a custom loader). */
typedef struct DefinitionRecordStruct
{
    AssetDefinition* Definition; // owned; the map key aliases Definition->Name
    unsigned char* SourcePath;   // owned; the file the definition was read from, NULL if registered in code
    uint64_t ReadStamp;          // the (ReadDefinitions pass, search root) that registered it; 0 if code
    Error LoadFailure;           // owned; success unless the last load attempt failed (cached until cleared)
} DefinitionRecord;

/* Where a definition being registered comes from. */
typedef struct DefinitionSourceStruct
{
    const unsigned char* Path; // borrowed; NULL when registered in code
    uint64_t ReadStamp;        // stamp of the search root being read; DEFINITION_READ_STAMP_CODE in code
    uint64_t PassFirstStamp;   // first stamp of the current ReadDefinitions pass; unused in code
} DefinitionSource;

/* Working state of one AssetManager_ReadDefinitions pass. */
typedef struct DefinitionScanStruct
{
    GenericBuffer DirectoryPath; // scratch: the type directory being scanned
    GenericBuffer FileBytes;     // scratch: the definition file being read
    GenericBuffer FileExtension; // scratch: definition-extension test
    uint64_t PassFirstStamp;     // first read stamp minted by this pass
    size_t FailedFileCount;      // definition files skipped so far
    Error FirstError;            // owned; the first directory-enumeration or structural failure
} DefinitionScan;

/* Scratch and results of resolving one File location to a file (see AssetManager_OpenResource). */
typedef struct FileResolutionStruct
{
    GenericBuffer Combined;        // root + type directory + relative path
    GenericBuffer Normalized;      // Combined with fixed separators
    GenericBuffer ParentDirectory; // the directory searched in the current root
    GenericBuffer TargetName;      // the location's last entry name; candidates have this stem
    GenericBuffer FileStem;        // scratch per file
    GenericBuffer FileExtension;   // scratch per file
    GenericBuffer CandidateNames;  // listed names of the deciding root's candidates
    GenericBuffer RejectedNames;   // listed names with the target stem but an unaccepted extension
    size_t CandidateCount;         // candidates found in the current root
} FileResolution;

/* A currently-loaded asset plus its holders and dependency user. */
typedef struct LoadedAssetRecordStruct
{
    AssetTypeID Type;
    unsigned char* Name;        // owned; the map key aliases this
    LoadedAsset Loaded;
    HashSet Holders;            // set of AssetUserID
    AssetUserID DependencyUser; // user under which this asset's dependencies are held
} LoadedAssetRecord;

/* A named in-memory resource registered via AssetManager_SetReferenceBytes. */
typedef struct ReferenceBlobStruct
{
    unsigned char* Name;  // owned
    unsigned char* Bytes; // owned; may be NULL when Count == 0
    size_t Count;
} ReferenceBlob;

/* A stream returned by AssetManager_OpenResource, plus any manager-owned backing buffer. The active
   concrete stream is the first member, so an IOStream* returned from it converts back to OpenedResource*. */
typedef struct OpenedResourceStruct
{
    union
    {
        FileStream File;
        MemoryStream Memory;
    } Stream;
    bool IsFile;
    GenericBuffer BackingBytes; // valid when HasBacking (memory streams over reference bytes)
    bool HasBacking;
} OpenedResource;

struct AssetResourcePathStruct
{
    unsigned char* Path; // owned, NUL-terminated
    bool IsTemp;         // if true, the path names a temp file to delete on release
};

struct AssetLoadProgressStruct
{
    size_t Total;
    size_t Processed;
};

struct PromisedAssetStruct
{
    AssetTypeID Type;
    unsigned char* Name; // owned
    void* Asset;         // resolved wrapper, or NULL
    bool Resolved;
    Error LoadError;     // owned; success Error if the entry loaded fine
};

struct AssetBulkOperationStruct
{
    AssetManager* Manager;       // borrowed
    AssetUserID User;
    ArrayList Entries;           // PromisedAsset*
    size_t NextIndex;
    AssetLoadProgress Progress;
};

struct AssetManagerStruct
{
    ArrayList _types;            // TypeRecord*
    HashMap _definitions;        // AssetKey -> DefinitionRecord (by value)
    HashMap _loadedAssets;       // AssetKey -> LoadedAssetRecord*
    ArrayList _searchRoots;      // unsigned char* (owned)
    ArrayList _referenceBlobs;   // ReferenceBlob* (owned)
    HashSet _activeUsers;        // set of AssetUserID
    AssetUserID _nextUserID;
    AssetTypeID _nextTypeID;
    WRBufferPool _bufferPool;
    unsigned char* _cacheDirectory; // owned
    uint64_t _tempFileCounter;
    AssetReferenceResolver _referenceResolver;
    UserData _referenceResolverUserData;
    StandardAssetTypes _standardTypes;
    Logger* _logger;             // borrowed; may be NULL (then nothing is logged)
    uint64_t _lastReadStamp;     // last stamp minted for a (ReadDefinitions pass, search root) pair
    size_t _loadFailureCount;    // number of definition records holding a cached load failure
};


// Static functions.
static Error CreateNullError(const unsigned char* parameterName)
{
    return Error_Construct3(ErrorCode_IllegalArgument, u8"Parameter \"%s\" cannot be null.", parameterName);
}

static const unsigned char* GetErrorMessage(const Error* error)
{
    return (error->Message != NULL) ? error->Message : ERROR_MESSAGE_FALLBACK;
}

/* Keeps the first non-success error in @p first and releases every later one. */
static void KeepFirstError(Error* first, Error* candidate)
{
    if (candidate->Code == ErrorCode_Success)
    {
        return;
    }
    if (first->Code == ErrorCode_Success)
    {
        *first = *candidate;
        *candidate = Error_CreateSuccess();
        return;
    }
    Error_Deconstruct(candidate);
}

static unsigned char* DuplicateString(const unsigned char* source)
{
    if (source == NULL)
    {
        return NULL;
    }

    size_t Length = StringUTF8_GetByteLength(source);
    unsigned char* Copy = Memory_Allocate(Length + 1U);
    Memory_Copy(source, Copy, Length + 1U);
    return Copy;
}

static bool StringsEqual(const unsigned char* a, const unsigned char* b)
{
    bool Equal = false;
    Error Result = StringUTF8_EqualsExact(a, b, &Equal);
    if (Result.Code != ErrorCode_Success) { Error_Deconstruct(&Result); return false; }
    return Equal;
}

static unsigned char ToLowerAscii(unsigned char character)
{
    if ((character >= (unsigned char)u8'A') && (character <= (unsigned char)u8'Z'))
    {
        return (unsigned char)(character + ((unsigned char)u8'a' - (unsigned char)u8'A'));
    }
    return character;
}

/* Compares two NUL-terminated strings, folding only ASCII letters. Bytes of multi-byte UTF-8 sequences are
   all >= 0x80, so they are compared exactly and the comparison stays valid for UTF-8 input. */
static bool EqualsIgnoreAsciiCase(const unsigned char* a, const unsigned char* b)
{
    const unsigned char* Left = a;
    const unsigned char* Right = b;
    while ((*Left != (unsigned char)u8'\0') && (*Right != (unsigned char)u8'\0'))
    {
        if (ToLowerAscii(*Left) != ToLowerAscii(*Right))
        {
            return false;
        }
        Left++;
        Right++;
    }
    return (*Left == (unsigned char)u8'\0') && (*Right == (unsigned char)u8'\0');
}

static bool IsAsciiLowercaseLetter(unsigned char character)
{
    return (character >= (unsigned char)u8'a') && (character <= (unsigned char)u8'z');
}

static bool IsAsciiDigit(unsigned char character)
{
    return (character >= (unsigned char)u8'0') && (character <= (unsigned char)u8'9');
}

/* Appends @p name to a message list ("a, b, c"). @p list is kept as one NUL-terminated growing string. */
static Error AppendListedName(GenericBuffer* list, const unsigned char* name)
{
    if (list->_count > 0U)
    {
        Error SeparatorResult = StringUTF8_CopyTo(NAME_LIST_SEPARATOR, list);
        if (SeparatorResult.Code != ErrorCode_Success) { return SeparatorResult; }
    }
    return StringUTF8_CopyTo(StringUTF8_IsEncodingValid(name) ? name : INVALID_FILE_NAME_PLACEHOLDER, list);
}

static HashCode HashAssetKey(IMap* map, const void* key, const UserData* userData)
{
    (void)map;
    (void)userData;
    const AssetKey* Key = key;
    HashCode TypeHash = Hash_UInt64((uint64_t)Key->Type);
    HashCode NameHash = Hash_String(Key->Name);
    // Mix the two so distinct (type, name) rarely collide.
    return TypeHash ^ (NameHash + (HashCode)0x9E3779B97F4A7C15ULL + (TypeHash << 6) + (TypeHash >> 2));
}

static bool CompareAssetKey(IMap* map, const void* key1, const void* key2, const UserData* userData)
{
    (void)map;
    (void)userData;
    const AssetKey* KeyA = key1;
    const AssetKey* KeyB = key2;
    if (KeyA->Type != KeyB->Type)
    {
        return false;
    }
    return StringsEqual(KeyA->Name, KeyB->Name);
}

/* AssetUserID sets are backed by a HashSet, wrapped in small helpers for the typed uint64 element. */
static HashCode HashUserID(ISet* set, const void* element, const UserData* userData)
{
    (void)set;
    (void)userData;
    return Hash_UInt64(*(const uint64_t*)element);
}

static Error ConstructUserSet(HashSet* set)
{
    HashSetConstructOptions Options = HashSetConstructOptions_CreateDefault(sizeof(AssetUserID), HashUserID);
    return HashSet_Construct1(set, Options);
}

static Error UserSetAdd(HashSet* set, AssetUserID user, bool* outAdded)
{
    return ISet_Add(HashSet_AsSet(set), &user, outAdded);
}

static Error UserSetRemove(HashSet* set, AssetUserID user, bool* outRemoved)
{
    return ISet_Remove(HashSet_AsSet(set), &user, outRemoved);
}

static bool UserSetContains(HashSet* set, AssetUserID user)
{
    bool Contains = false;
    Error Result = ISet_Contains(HashSet_AsSet(set), &user, &Contains);
    if (Result.Code != ErrorCode_Success) { Error_Deconstruct(&Result); return false; }
    return Contains;
}

static size_t UserSetCount(HashSet* set)
{
    return ISet_GetElementCount(HashSet_AsSet(set));
}

static IList* TypesList(AssetManager* self)
{
    return &self->_types._list;
}

static IList* RootsList(AssetManager* self)
{
    return &self->_searchRoots._list;
}

static IList* ReferenceList(AssetManager* self)
{
    return &self->_referenceBlobs._list;
}

static IMap* DefinitionsMap(AssetManager* self)
{
    return HashMap_AsMap(&self->_definitions);
}

static IMap* LoadedMap(AssetManager* self)
{
    return HashMap_AsMap(&self->_loadedAssets);
}

static TypeRecord* FindTypeByID(AssetManager* self, AssetTypeID id)
{
    IList* List = TypesList(self);
    size_t Count = IList_GetElementCount(List);
    for (size_t i = 0; i < Count; i++)
    {
        TypeRecord* Record = NULL;
        Error Result = IList_GetElement(List, i, &Record);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            continue;
        }
        if (Record->ID == id)
        {
            return Record;
        }
    }
    return NULL;
}

static TypeRecord* FindTypeByNameOrDirectory(AssetManager* self, const unsigned char* name, const unsigned char* directory)
{
    IList* List = TypesList(self);
    size_t Count = IList_GetElementCount(List);
    for (size_t i = 0; i < Count; i++)
    {
        TypeRecord* Record = NULL;
        Error Result = IList_GetElement(List, i, &Record);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            continue;
        }
        if ((name != NULL) && StringsEqual(Record->Name, name))
        {
            return Record;
        }
        if ((directory != NULL) && StringsEqual(Record->DirectoryName, directory))
        {
            return Record;
        }
    }
    return NULL;
}

static ReferenceBlob* FindReferenceBlob(AssetManager* self, const unsigned char* name)
{
    IList* List = ReferenceList(self);
    size_t Count = IList_GetElementCount(List);
    for (size_t i = 0; i < Count; i++)
    {
        ReferenceBlob* Blob = NULL;
        Error Result = IList_GetElement(List, i, &Blob);
        if (Result.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Result);
            continue;
        }
        if (StringsEqual(Blob->Name, name))
        {
            return Blob;
        }
    }
    return NULL;
}

/* Looks up a definition record. The pointer aliases map storage: see DefinitionRecord for its lifetime.
   Checks presence first because a missed IMap lookup allocates an error message. */
static bool TryGetDefinitionRecord(AssetManager* self, AssetTypeID type, const unsigned char* name, DefinitionRecord** outRecord)
{
    *outRecord = NULL;
    AssetKey Key = { type, name };
    bool Contains = false;
    Error Result = IMap_ContainsKey(DefinitionsMap(self), &Key, &Contains);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false;
    }
    if (!Contains)
    {
        return false;
    }
    void* RecordPointer = NULL;
    Error GetResult = IMap_GetPointerToElement(DefinitionsMap(self), &Key, &RecordPointer);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return false;
    }
    *outRecord = RecordPointer;
    return true;
}

static bool TryGetLoadedRecord(AssetManager* self, AssetTypeID type, const unsigned char* name, LoadedAssetRecord** outRecord)
{
    AssetKey Key = { type, name };
    bool Contains = false;
    Error Result = IMap_ContainsKey(LoadedMap(self), &Key, &Contains);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return false;
    }
    if (!Contains)
    {
        return false;
    }
    Error GetResult = IMap_GetElement(LoadedMap(self), &Key, outRecord);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return false;
    }
    return true;
}

/* Reports whether @p map (keyed by AssetKey) holds any entry of @p type. */
static Error MapHasKeyOfType(IMap* map, AssetTypeID type, bool* outFound)
{
    *outFound = false;
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(IMap_AsKeyCollection(map));
    if (Enumerator == NULL)
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to enumerate asset keys.");
    }

    Error Result = Error_CreateSuccess();
    while (true)
    {
        bool HasNext = false;
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code != ErrorCode_Success) || !HasNext)
        {
            break;
        }

        AssetKey Key;
        Result = CollectionEnumerator_NextByValue(Enumerator, &Key);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        if (Key.Type == type)
        {
            *outFound = true;
            break;
        }
    }

    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}

static bool CanDestroyDefinition(const AssetDefinition* definition)
{
    return (definition->VTable != NULL) && (definition->VTable->Destroy != NULL);
}

/* Releases everything a definition record owns (the record itself lives in, or was copied out of, the map). */
static void ReleaseDefinitionRecord(AssetManager* self, DefinitionRecord* record)
{
    if (record->LoadFailure.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&record->LoadFailure);
        if (self->_loadFailureCount > 0U)
        {
            self->_loadFailureCount--;
        }
    }
    if ((record->Definition != NULL) && CanDestroyDefinition(record->Definition))
    {
        record->Definition->VTable->Destroy(record->Definition);
    }
    record->Definition = NULL;
    Memory_Free(record->SourcePath);
    record->SourcePath = NULL;
}

/* Removes the (type, name) definition and releases its record. */
static Error RemoveDefinitionRecord(AssetManager* self, AssetTypeID type, const unsigned char* name)
{
    DefinitionRecord* RecordPointer = NULL;
    if (!TryGetDefinitionRecord(self, type, name, &RecordPointer))
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"No definition named \"%s\" for the type.", name);
    }

    // Copy the record out first: removing can rebuild the map storage the pointer aliases.
    DefinitionRecord Removed = *RecordPointer;
    AssetKey Key = { type, Removed.Definition->Name };
    bool WasRemoved = false;
    Error RemoveResult = IMap_Remove(DefinitionsMap(self), &Key, &WasRemoved);
    if (WasRemoved)
    {
        // The entry is gone even if a storage rebuild after the removal reported an error.
        ReleaseDefinitionRecord(self, &Removed);
    }
    return RemoveResult;
}

/* Forgets every cached load failure. O(1) when nothing failed. */
static Error ClearAllLoadFailures(AssetManager* self)
{
    if (self->_loadFailureCount == 0U)
    {
        return Error_CreateSuccess();
    }

    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(IMap_AsValueCollection(DefinitionsMap(self)));
    if (Enumerator == NULL)
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to enumerate asset definitions.");
    }

    Error Result = Error_CreateSuccess();
    while (true)
    {
        bool HasNext = false;
        Result = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code != ErrorCode_Success) || !HasNext)
        {
            break;
        }

        void* RecordPointer = NULL;
        Result = CollectionEnumerator_NextByReference(Enumerator, &RecordPointer);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        DefinitionRecord* Record = RecordPointer;
        if (Record->LoadFailure.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&Record->LoadFailure);
            if (self->_loadFailureCount > 0U)
            {
                self->_loadFailureCount--;
            }
        }
    }

    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}

/* Remembers a failed build on its definition and logs it once. */
static void CacheLoadFailure(AssetManager* self, const TypeRecord* type, const unsigned char* name, const Error* loadError)
{
    const unsigned char* SourcePath = NULL;
    DefinitionRecord* Record = NULL;
    // Look the record up again: a custom loader may have changed the definitions map during the load.
    if (TryGetDefinitionRecord(self, type->ID, name, &Record))
    {
        if (Record->LoadFailure.Code == ErrorCode_Success)
        {
            self->_loadFailureCount++;
        }
        else
        {
            Error_Deconstruct(&Record->LoadFailure);
        }
        Record->LoadFailure = Error_Construct1(loadError->Code, loadError->Message);
        SourcePath = Record->SourcePath;
    }

    if (self->_logger == NULL)
    {
        return;
    }
    Error LogResult = Error_CreateSuccess();
    if (SourcePath != NULL)
    {
        LogResult = Logger_LogErrorFormatted(self->_logger, u8"Failed to load %s \"%s\" (defined in \"%s\"): %s",
            type->Name, name, SourcePath, GetErrorMessage(loadError));
    }
    else
    {
        LogResult = Logger_LogErrorFormatted(self->_logger, u8"Failed to load %s \"%s\" (defined in code): %s",
            type->Name, name, GetErrorMessage(loadError));
    }
    Error_Deconstruct(&LogResult);
}

/* Registers @p definition, ALWAYS taking ownership: on every failure path it is destroyed (except when it
   has no destroy function: then nothing can destroy it, so it is left to the caller). @p source says where
   it came from:
   - In code (ReadStamp DEFINITION_READ_STAMP_CODE): replaces any existing definition of the same (type, name).
   - From a file: a definition registered earlier from the SAME root in the same pass is a duplicate error
     naming both files; one from a higher-priority root of this pass, or one registered in code, wins (the
     new one is dropped silently); one left over from an earlier pass is replaced. */
static Error RegisterDefinition(AssetManager* self, AssetDefinition* definition, const DefinitionSource* source)
{
    if (!CanDestroyDefinition(definition))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Definition has no destroy function; ownership stays with the caller.");
    }

    TypeRecord* Type = FindTypeByID(self, definition->Type);
    if (Type == NULL)
    {
        definition->VTable->Destroy(definition);
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Definition has an unregistered type id.");
    }

    if (!AssetManager_IsValidAssetName(definition->Name))
    {
        // Build the message before destroying the definition: the name belongs to it.
        Error NameResult = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Invalid %s name \"%s\": a name starts with a lowercase letter (a-z) and contains only a-z, 0-9, '_', '.' and '-'.",
            Type->Name, (definition->Name != NULL) ? definition->Name : (const unsigned char*)u8"");
        definition->VTable->Destroy(definition);
        return NameResult;
    }

    DefinitionRecord* Existing = NULL;
    if (TryGetDefinitionRecord(self, definition->Type, definition->Name, &Existing))
    {
        if (source->ReadStamp != DEFINITION_READ_STAMP_CODE)
        {
            if (Existing->ReadStamp == source->ReadStamp)
            {
                Error DuplicateResult = Error_Construct3(ErrorCode_InvalidAssetDefinition,
                    u8"Duplicate %s definition \"%s\" in one search root: \"%s\" and \"%s\".",
                    Type->Name, definition->Name,
                    (Existing->SourcePath != NULL) ? Existing->SourcePath : (const unsigned char*)u8"?",
                    (source->Path != NULL) ? source->Path : (const unsigned char*)u8"?");
                definition->VTable->Destroy(definition);
                return DuplicateResult;
            }
            if ((Existing->ReadStamp == DEFINITION_READ_STAMP_CODE) || (Existing->ReadStamp >= source->PassFirstStamp))
            {
                // A higher-priority root of this pass (the override mechanism) or code already provides it.
                definition->VTable->Destroy(definition);
                return Error_CreateSuccess();
            }
        }

        Error RemoveResult = RemoveDefinitionRecord(self, definition->Type, definition->Name);
        if (RemoveResult.Code != ErrorCode_Success)
        {
            definition->VTable->Destroy(definition);
            return RemoveResult;
        }
    }

    DefinitionRecord Record;
    Record.Definition = definition;
    Record.SourcePath = DuplicateString(source->Path);
    Record.ReadStamp = source->ReadStamp;
    Record.LoadFailure = Error_CreateSuccess();

    AssetKey Key = { definition->Type, definition->Name };
    bool WasAdded = false;
    Error AddResult = IMap_Add(DefinitionsMap(self), &Key, &Record, &WasAdded);
    if (AddResult.Code != ErrorCode_Success)
    {
        Memory_Free(Record.SourcePath);
        definition->VTable->Destroy(definition);
        return AddResult;
    }
    return Error_CreateSuccess();
}

static bool IsUserActive(AssetManager* self, AssetUserID user)
{
    if (user == ASSET_USER_ID_INVALID)
    {
        return false;
    }
    return UserSetContains(&self->_activeUsers, user);
}

static Error MintUser(AssetManager* self, AssetUserID* outUser)
{
    AssetUserID ID = self->_nextUserID;
    bool WasAdded = false;
    Error Result = UserSetAdd(&self->_activeUsers, ID, &WasAdded);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_nextUserID++;
    *outUser = ID;
    return Error_CreateSuccess();
}

static void RemoveActiveUser(AssetManager* self, AssetUserID user)
{
    bool WasRemoved = false;
    Error Result = UserSetRemove(&self->_activeUsers, user, &WasRemoved);
    Error_Deconstruct(&Result);
}

/* Removes @p user from every loaded asset's holder set. Only mutates per-record sets, never the loaded
   map, so it is safe to run while enumerating the map. */
static Error RemoveUserFromAllHolders(AssetManager* self, AssetUserID user)
{
    ICollection* Values = IMap_AsValueCollection(LoadedMap(self));
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(Values);
    if (Enumerator == NULL)
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to enumerate loaded assets.");
    }

    Error Result = Error_CreateSuccess();
    while (true)
    {
        bool HasNext = false;
        Error NextCheck = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if (NextCheck.Code != ErrorCode_Success)
        {
            Result = NextCheck;
            break;
        }
        if (!HasNext)
        {
            break;
        }

        LoadedAssetRecord* Record = NULL;
        Error NextResult = CollectionEnumerator_NextByValue(Enumerator, &Record);
        if (NextResult.Code != ErrorCode_Success)
        {
            Result = NextResult;
            break;
        }

        bool WasRemoved = false;
        Error RemoveResult = UserSetRemove(&Record->Holders, user, &WasRemoved);
        if (RemoveResult.Code != ErrorCode_Success)
        {
            Result = RemoveResult;
            break;
        }
    }

    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}

/* Finds the first loaded record with zero holders (or, when @p anyRecord, simply the first record).
   Enumeration is read-only; the caller stops before mutating the map. */
static Error FindRecord(AssetManager* self, bool anyRecord, LoadedAssetRecord** outRecord)
{
    *outRecord = NULL;
    ICollection* Values = IMap_AsValueCollection(LoadedMap(self));
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(Values);
    if (Enumerator == NULL)
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to enumerate loaded assets.");
    }

    Error Result = Error_CreateSuccess();
    while (true)
    {
        bool HasNext = false;
        Error NextCheck = CollectionEnumerator_HasNext(Enumerator, &HasNext);
        if (NextCheck.Code != ErrorCode_Success)
        {
            Result = NextCheck;
            break;
        }
        if (!HasNext)
        {
            break;
        }

        LoadedAssetRecord* Record = NULL;
        Error NextResult = CollectionEnumerator_NextByValue(Enumerator, &Record);
        if (NextResult.Code != ErrorCode_Success)
        {
            Result = NextResult;
            break;
        }

        if (anyRecord || (UserSetCount(&Record->Holders) == 0))
        {
            *outRecord = Record;
            break;
        }
    }

    CollectionEnumerator_Destroy(Enumerator);
    return Result;
}

/* Forward declaration: UnloadRecord releases dependencies via ReleaseAllAssetsForUser and vice-versa. */
static Error UnloadRecord(AssetManager* self, LoadedAssetRecord* record);

static Error ReleaseAllForUser(AssetManager* self, AssetUserID user)
{
    Error RemoveResult = RemoveUserFromAllHolders(self, user);
    if (RemoveResult.Code != ErrorCode_Success)
    {
        return RemoveResult;
    }

    while (true)
    {
        LoadedAssetRecord* Record = NULL;
        Error FindResult = FindRecord(self, false, &Record);
        if (FindResult.Code != ErrorCode_Success)
        {
            return FindResult;
        }
        if (Record == NULL)
        {
            break;
        }

        Error UnloadResult = UnloadRecord(self, Record);
        if (UnloadResult.Code != ErrorCode_Success)
        {
            return UnloadResult;
        }
    }
    return Error_CreateSuccess();
}

static Error UnloadRecord(AssetManager* self, LoadedAssetRecord* record)
{
    // Remove from the map first so nothing can look it up again during teardown.
    AssetKey Key = { record->Type, record->Name };
    bool WasRemoved = false;
    Error RemoveResult = IMap_Remove(LoadedMap(self), &Key, &WasRemoved);
    if (RemoveResult.Code != ErrorCode_Success)
    {
        return RemoveResult;
    }

    // Tear the wrapper down before its dependencies (it may reference their data, e.g. textures).
    if ((record->Loaded.VTable != NULL) && (record->Loaded.VTable->Destroy != NULL))
    {
        record->Loaded.VTable->Destroy(&record->Loaded, self);
    }

    // Drop this asset's dependency holds; any dependency that reaches zero holders unloads here.
    Error DependencyResult = ReleaseAllForUser(self, record->DependencyUser);

    RemoveActiveUser(self, record->DependencyUser);

    Error DeconstructResult = HashSet_Deconstruct(&record->Holders);
    Memory_Free(record->Name);
    Memory_Free(record);

    if (DependencyResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&DeconstructResult);
        return DependencyResult;
    }
    return DeconstructResult;
}

static Error EnsureCacheDirectory(AssetManager* self)
{
    return FileSystem_CreateAllDirectories(self->_cacheDirectory);
}

static Error ClearCacheDirectory(AssetManager* self)
{
    DirectoryEntryEnumerator* Enumerator = NULL;
    Error GetResult = FileSystem_GetEntries(self->_cacheDirectory, DirectorySearchOption_TopLevel, &Enumerator);
    if ((GetResult.Code == ErrorCode_DirectoryNotFound) || (GetResult.Code == ErrorCode_FileNotFound))
    {
        Error_Deconstruct(&GetResult);
        return Error_CreateSuccess();
    }
    if (GetResult.Code != ErrorCode_Success)
    {
        return GetResult;
    }

    Error Result = Error_CreateSuccess();
    while (true)
    {
        bool HasNext = false;
        Error NextCheck = DirectoryEntryEnumerator_HasNext(Enumerator, &HasNext);
        if (NextCheck.Code != ErrorCode_Success)
        {
            KeepFirstError(&Result, &NextCheck);
            break;
        }
        if (!HasNext)
        {
            break;
        }

        FileSystemEntryInfo Info;
        Memory_Zero(&Info, sizeof(Info));
        Error NextResult = DirectoryEntryEnumerator_Next(Enumerator, &Info);
        if (NextResult.Code != ErrorCode_Success)
        {
            KeepFirstError(&Result, &NextResult);
            break;
        }

        Error DeleteResult = FileSystem_DeleteEntry(Info._path);
        KeepFirstError(&Result, &DeleteResult);
        FileSystemEntryInfo_Deconstruct(&Info);
    }

    Error DeconstructResult = DirectoryEntryEnumerator_Deconstruct(Enumerator);
    KeepFirstError(&Result, &DeconstructResult);
    return Result;
}

/* True when @p path is one of @p type's definition files. AssetManager_ReadDefinitions treats every file
   with the type's definition extension as a definition, so such a file can never also be a resource. */
static bool IsDefinitionFile(TypeRecord* type, const unsigned char* path, GenericBuffer* extensionScratch)
{
    if (type->DefinitionFileExtension == NULL)
    {
        return false;
    }

    GenericBuffer_Clear(extensionScratch);
    Error ExtensionResult = Path_GetExtension(path, extensionScratch);
    bool IsDefinition = (ExtensionResult.Code == ErrorCode_Success)
        && (extensionScratch->_data != NULL)
        && (extensionScratch->_data[0] == (unsigned char)u8'.')
        && EqualsIgnoreAsciiCase(extensionScratch->_data + 1, type->DefinitionFileExtension);
    Error_Deconstruct(&ExtensionResult);
    return IsDefinition;
}

static void FileResolution_Construct(FileResolution* self)
{
    GenericBuffer_AllocateVariable(&self->Combined, 0U, 1U);
    GenericBuffer_AllocateVariable(&self->Normalized, 0U, 1U);
    GenericBuffer_AllocateVariable(&self->ParentDirectory, 0U, 1U);
    GenericBuffer_AllocateVariable(&self->TargetName, 0U, 1U);
    GenericBuffer_AllocateVariable(&self->FileStem, 0U, 1U);
    GenericBuffer_AllocateVariable(&self->FileExtension, 0U, 1U);
    GenericBuffer_AllocateVariable(&self->CandidateNames, 0U, 1U);
    GenericBuffer_AllocateVariable(&self->RejectedNames, 0U, 1U);
    self->CandidateCount = 0U;
}

static void FileResolution_Deconstruct(FileResolution* self)
{
    Memory_Free(self->Combined._data);
    Memory_Free(self->Normalized._data);
    Memory_Free(self->ParentDirectory._data);
    Memory_Free(self->TargetName._data);
    Memory_Free(self->FileStem._data);
    Memory_Free(self->FileExtension._data);
    Memory_Free(self->CandidateNames._data);
    Memory_Free(self->RejectedNames._data);
}

/* Builds the directory a File location points into for one search root, into resolution->ParentDirectory. */
static Error BuildCandidateDirectory(const unsigned char* root, const TypeRecord* type, const unsigned char* relativePath,
    FileResolution* resolution)
{
    GenericBuffer_Clear(&resolution->Combined);
    const unsigned char* Parts[3] = { root, type->DirectoryName, relativePath };
    Error Result = Path_Combine(Parts, 3U, &resolution->Combined);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    GenericBuffer_Clear(&resolution->Normalized);
    Result = Path_Normalize(resolution->Combined._data, PathNormalizeConditions_FixSeparators, &resolution->Normalized);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    GenericBuffer_Clear(&resolution->ParentDirectory);
    return Path_GetParentPath(resolution->Normalized._data, &resolution->ParentDirectory);
}

/* Classifies one directory entry: a candidate (stem matches, not a definition file, extension accepted),
   a rejected match (stem matches, extension not accepted), or unrelated. The first candidate's path goes
   to @p outFullPath. */
static Error ConsiderCandidateFile(const TypeRecord* type, const AssetExtensionFilter* extensions,
    const FileSystemEntryInfo* info, FileResolution* resolution, GenericBuffer* outFullPath)
{
    GenericBuffer_Clear(&resolution->FileStem);
    Error StemResult = Path_GetLastEntryStem(info->_name, &resolution->FileStem);
    if (StemResult.Code != ErrorCode_Success)
    {
        // A name the path functions cannot parse can never match a location; skip it.
        Error_Deconstruct(&StemResult);
        return Error_CreateSuccess();
    }
    if (!StringsEqual(resolution->FileStem._data, resolution->TargetName._data))
    {
        return Error_CreateSuccess();
    }

    GenericBuffer_Clear(&resolution->FileExtension);
    Error ExtensionResult = Path_GetExtension(info->_name, &resolution->FileExtension);
    if ((ExtensionResult.Code != ErrorCode_Success) || (resolution->FileExtension._data == NULL))
    {
        Error_Deconstruct(&ExtensionResult);
        return Error_CreateSuccess();
    }
    const unsigned char* Extension = resolution->FileExtension._data;
    if (Extension[0] == (unsigned char)u8'.')
    {
        Extension++;
    }

    if ((type->DefinitionFileExtension != NULL) && EqualsIgnoreAsciiCase(Extension, type->DefinitionFileExtension))
    {
        // A definition may share its stem with the resource it defines (test.json + test.png).
        return Error_CreateSuccess();
    }
    if (!AssetExtensionFilter_Accepts(extensions, Extension))
    {
        return AppendListedName(&resolution->RejectedNames, info->_name);
    }

    resolution->CandidateCount++;
    if (resolution->CandidateCount == 1U)
    {
        GenericBuffer_Clear(outFullPath);
        Error CopyResult = StringUTF8_CopyTo(info->_path, outFullPath);
        if (CopyResult.Code != ErrorCode_Success)
        {
            return CopyResult;
        }
    }
    return AppendListedName(&resolution->CandidateNames, info->_name);
}

/* Scans resolution->ParentDirectory for candidates. A missing directory simply has none. */
static Error CollectDirectoryCandidates(const TypeRecord* type, const AssetExtensionFilter* extensions,
    FileResolution* resolution, GenericBuffer* outFullPath)
{
    DirectoryEntryEnumerator* Enumerator = NULL;
    Error FilesResult = FileSystem_GetFiles(resolution->ParentDirectory._data, DirectorySearchOption_TopLevel, &Enumerator);
    if ((FilesResult.Code == ErrorCode_DirectoryNotFound) || (FilesResult.Code == ErrorCode_FileNotFound))
    {
        Error_Deconstruct(&FilesResult);
        return Error_CreateSuccess();
    }
    if (FilesResult.Code != ErrorCode_Success)
    {
        return FilesResult;
    }

    Error Result = Error_CreateSuccess();
    while (true)
    {
        bool HasNext = false;
        Result = DirectoryEntryEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code != ErrorCode_Success) || !HasNext)
        {
            break;
        }

        FileSystemEntryInfo Info;
        Memory_Zero(&Info, sizeof(Info));
        Result = DirectoryEntryEnumerator_Next(Enumerator, &Info);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        Result = ConsiderCandidateFile(type, extensions, &Info, resolution, outFullPath);
        FileSystemEntryInfo_Deconstruct(&Info);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
    }

    Error DeconstructResult = DirectoryEntryEnumerator_Deconstruct(Enumerator);
    KeepFirstError(&Result, &DeconstructResult);
    return Result;
}

/* Turns the outcome of a resolution into its result: success for exactly one candidate, otherwise the
   ambiguity, unsupported-format or not-found error. */
static Error FinishFileResolution(const TypeRecord* type, const unsigned char* location,
    const AssetExtensionFilter* extensions, FileResolution* resolution)
{
    if (resolution->CandidateCount == 1U)
    {
        return Error_CreateSuccess();
    }
    if (resolution->CandidateCount > 1U)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"Location \"%s\" is ambiguous: these files in \"%s\" all match it: %s. Keep only one of them (or pick one with \"format\" where the asset type supports it).",
            location, resolution->ParentDirectory._data, resolution->CandidateNames._data);
    }
    if (resolution->RejectedNames._count > 0U)
    {
        // Combined is free scratch at this point.
        GenericBuffer_Clear(&resolution->Combined);
        Error ListResult = AssetExtensionFilter_AppendList(extensions, &resolution->Combined);
        if (ListResult.Code != ErrorCode_Success)
        {
            return ListResult;
        }
        return Error_Construct3(ErrorCode_InvalidAssetData,
            u8"No usable file for location \"%s\": found %s, but this asset type only accepts %s.",
            location, resolution->RejectedNames._data, resolution->Combined._data);
    }
    return Error_Construct3(ErrorCode_FileNotFound,
        u8"No file found for location \"%s\" in the \"%s\" directory of any search root.", location, type->DirectoryName);
}

/* Resolves a File location to exactly one existing file (the rule is documented on AssetManager_OpenResource).
   On success @p outFullPath holds the file's full path. */
static Error ResolveExistingFilePath(AssetManager* self, TypeRecord* type, const unsigned char* relativePath,
    const AssetExtensionFilter* extensions, GenericBuffer* outFullPath)
{
    const unsigned char* Relative = relativePath;
    if ((Relative[0] == (unsigned char)u8'/') || (Relative[0] == (unsigned char)u8'\\'))
    {
        Relative++;
    }

    FileResolution Resolution;
    FileResolution_Construct(&Resolution);

    // The location's last entry NAME is the target stem, so "a/b.c" looks for "b.c.*", never "b.*".
    Error Result = Path_GetLastEntryName(Relative, &Resolution.TargetName);
    size_t RootCount = IList_GetElementCount(RootsList(self));
    for (size_t i = 0; (i < RootCount) && (Result.Code == ErrorCode_Success) && (Resolution.CandidateCount == 0U); i++)
    {
        unsigned char* Root = NULL;
        Result = IList_GetElement(RootsList(self), i, &Root);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        Result = BuildCandidateDirectory(Root, type, Relative, &Resolution);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
        Result = CollectDirectoryCandidates(type, extensions, &Resolution, outFullPath);
    }

    if (Result.Code == ErrorCode_Success)
    {
        Result = FinishFileResolution(type, relativePath, extensions, &Resolution);
    }
    FileResolution_Deconstruct(&Resolution);
    return Result;
}

/* Appends the bytes of a reference resource (registered blob first, then the resolver) into @p outBuffer. */
static Error GetReferenceBytes(AssetManager* self, const unsigned char* name, GenericBuffer* outBuffer)
{
    ReferenceBlob* Blob = FindReferenceBlob(self, name);
    if (Blob != NULL)
    {
        if ((Blob->Count > 0U) && !GenericBuffer_AppendRangeBytes(outBuffer, Blob->Bytes, Blob->Count))
        {
            return Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to copy reference bytes for \"%s\".", name);
        }
        return Error_CreateSuccess();
    }

    if (self->_referenceResolver != NULL)
    {
        IOStream* Stream = NULL;
        Error ResolveResult = self->_referenceResolver(self, &self->_referenceResolverUserData, name, &Stream);
        if (ResolveResult.Code != ErrorCode_Success)
        {
            return ResolveResult;
        }
        if (Stream == NULL)
        {
            return Error_Construct3(ErrorCode_InvalidOperation, u8"Reference resolver returned no stream for \"%s\".", name);
        }

        Error ReadResult = IOStream_ReadAll(Stream, outBuffer);
        Error CloseResult = IOStream_Deconstruct(Stream);
        if (ReadResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&CloseResult);
            return ReadResult;
        }
        return CloseResult;
    }

    return Error_Construct3(ErrorCode_InvalidOperation, u8"No reference resource named \"%s\".", name);
}

static Error BuildTempFilePath(AssetManager* self, const unsigned char* extension, GenericBuffer* outPath)
{
    uint64_t Number = self->_tempFileCounter;
    self->_tempFileCounter++;

    GenericBuffer FileName;
    GenericBuffer_AllocateVariable(&FileName, 0U, 1U);

    // The number writer appends raw digits and then a NUL, so the prefix must not be NUL-terminated, and the
    // pieces after the number go through StringUTF8_CopyTo, which drops that NUL first. (Appending raw after
    // the number placed the extension behind the NUL and silently cut the name to "temp_<n>".)
    Error Result = Error_CreateSuccess();
    if (!GenericBuffer_AppendString(&FileName, (const unsigned char*)u8"temp_"))
    {
        Result = Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to build temp file name.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Number_UInt64ToString(Number, NUMBER_BASE_10, false, &FileName);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringUTF8_CopyTo((const unsigned char*)u8".", &FileName);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringUTF8_CopyTo(extension, &FileName);
    }

    if (Result.Code == ErrorCode_Success)
    {
        const unsigned char* Parts[2] = { self->_cacheDirectory, FileName._data };
        Result = Path_Combine(Parts, 2U, outPath);
    }

    Memory_Free(FileName._data);
    return Result;
}

/* Keeps the first scan failure to return, releases later ones, and logs every one with its directory. */
static void NoteScanFailure(AssetManager* self, DefinitionScan* scan, const TypeRecord* type,
    const unsigned char* directory, Error* failure)
{
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogErrorFormatted(self->_logger, u8"Could not scan %s definitions in \"%s\": %s",
            type->Name, (directory != NULL) ? directory : (const unsigned char*)u8"?", GetErrorMessage(failure));
        Error_Deconstruct(&LogResult);
    }
    KeepFirstError(&scan->FirstError, failure);
}

/* Reads, builds and registers one definition file. A failure is logged, counted and released; it never
   stops the scan. */
static void ReadDefinitionFile(AssetManager* self, TypeRecord* type, const unsigned char* path, uint64_t rootStamp,
    DefinitionScan* scan)
{
    GenericBuffer_Clear(&scan->FileBytes);
    Error Result = FileSystem_ReadAllBytes(path, &scan->FileBytes);
    if (Result.Code == ErrorCode_Success)
    {
        AssetDefinition* Definition = NULL;
        Result = AssetManager_BuildDefinition(self, type->ID, &scan->FileBytes, path, &Definition);
        if (Result.Code == ErrorCode_Success)
        {
            DefinitionSource Source = { path, rootStamp, scan->PassFirstStamp };
            Result = RegisterDefinition(self, Definition, &Source);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        return;
    }

    scan->FailedFileCount++;
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogErrorFormatted(self->_logger, u8"Skipped %s definition \"%s\": %s",
            type->Name, path, GetErrorMessage(&Result));
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(&Result);
}

/* Reads every definition file of @p type under @p root. An enumeration failure ends only this directory. */
static void ReadTypeDirectory(AssetManager* self, const unsigned char* root, TypeRecord* type, uint64_t rootStamp,
    DefinitionScan* scan)
{
    GenericBuffer_Clear(&scan->DirectoryPath);
    const unsigned char* Parts[2] = { root, type->DirectoryName };
    Error CombineResult = Path_Combine(Parts, 2U, &scan->DirectoryPath);
    if (CombineResult.Code != ErrorCode_Success)
    {
        NoteScanFailure(self, scan, type, root, &CombineResult);
        return;
    }

    DirectoryEntryEnumerator* Enumerator = NULL;
    Error FilesResult = FileSystem_GetFiles(scan->DirectoryPath._data, DirectorySearchOption_All, &Enumerator);
    if ((FilesResult.Code == ErrorCode_DirectoryNotFound) || (FilesResult.Code == ErrorCode_FileNotFound))
    {
        // A root need not provide every asset type.
        Error_Deconstruct(&FilesResult);
        return;
    }
    if (FilesResult.Code != ErrorCode_Success)
    {
        NoteScanFailure(self, scan, type, scan->DirectoryPath._data, &FilesResult);
        return;
    }

    Error Result = Error_CreateSuccess();
    while (true)
    {
        bool HasNext = false;
        Result = DirectoryEntryEnumerator_HasNext(Enumerator, &HasNext);
        if ((Result.Code != ErrorCode_Success) || !HasNext)
        {
            break;
        }

        FileSystemEntryInfo Info;
        Memory_Zero(&Info, sizeof(Info));
        Result = DirectoryEntryEnumerator_Next(Enumerator, &Info);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }

        // When the type declares a definition-file extension, only files with that extension are
        // definitions; everything else (e.g. resource files sharing the type directory) is skipped.
        if ((type->DefinitionFileExtension == NULL) || IsDefinitionFile(type, Info._path, &scan->FileExtension))
        {
            ReadDefinitionFile(self, type, Info._path, rootStamp, scan);
        }
        FileSystemEntryInfo_Deconstruct(&Info);
    }

    Error DeconstructResult = DirectoryEntryEnumerator_Deconstruct(Enumerator);
    KeepFirstError(&Result, &DeconstructResult);
    if (Result.Code != ErrorCode_Success)
    {
        NoteScanFailure(self, scan, type, scan->DirectoryPath._data, &Result);
    }
}

static void FreeTypeRecord(TypeRecord* record)
{
    Memory_Free(record->Name);
    Memory_Free(record->DirectoryName);
    Memory_Free(record->DefinitionFileExtension);
    Memory_Free(record);
}


// Public functions.
Error AssetManager_Construct1(Logger* logger, AssetManager** outSelf)
{
    if (outSelf == NULL)
    {
        return CreateNullError(u8"outSelf");
    }
    *outSelf = NULL;

    AssetManager* Self = Memory_Allocate(sizeof(AssetManager));
    Memory_Zero(Self, sizeof(*Self));

    ArrayList_Construct1(&Self->_types, sizeof(TypeRecord*));
    ArrayList_Construct1(&Self->_searchRoots, sizeof(unsigned char*));
    ArrayList_Construct1(&Self->_referenceBlobs, sizeof(ReferenceBlob*));

    HashMapConstructOptions DefinitionOptions = HashMapConstructOptions_CreateDefault(
        sizeof(AssetKey), sizeof(DefinitionRecord), HashAssetKey);
    DefinitionOptions.KeyComparator = CompareAssetKey;
    Error Result = HashMap_Construct1(&Self->_definitions, DefinitionOptions);
    if (Result.Code != ErrorCode_Success)
    {
        ArrayList_Deconstruct(&Self->_types);
        ArrayList_Deconstruct(&Self->_searchRoots);
        ArrayList_Deconstruct(&Self->_referenceBlobs);
        Memory_Free(Self);
        return Result;
    }

    HashMapConstructOptions LoadedOptions = HashMapConstructOptions_CreateDefault(
        sizeof(AssetKey), sizeof(LoadedAssetRecord*), HashAssetKey);
    LoadedOptions.KeyComparator = CompareAssetKey;
    Result = HashMap_Construct1(&Self->_loadedAssets, LoadedOptions);
    if (Result.Code != ErrorCode_Success)
    {
        Error DefinitionsResult = HashMap_Deconstruct(&Self->_definitions);
        Error_Deconstruct(&DefinitionsResult);
        ArrayList_Deconstruct(&Self->_types);
        ArrayList_Deconstruct(&Self->_searchRoots);
        ArrayList_Deconstruct(&Self->_referenceBlobs);
        Memory_Free(Self);
        return Result;
    }

    Result = ConstructUserSet(&Self->_activeUsers);
    if (Result.Code != ErrorCode_Success)
    {
        Error LoadedResult = HashMap_Deconstruct(&Self->_loadedAssets);
        Error_Deconstruct(&LoadedResult);
        Error DefinitionsResult = HashMap_Deconstruct(&Self->_definitions);
        Error_Deconstruct(&DefinitionsResult);
        ArrayList_Deconstruct(&Self->_types);
        ArrayList_Deconstruct(&Self->_searchRoots);
        ArrayList_Deconstruct(&Self->_referenceBlobs);
        Memory_Free(Self);
        return Result;
    }

    Result = BufferPool_Construct1(&Self->_bufferPool);
    if (Result.Code != ErrorCode_Success)
    {
        Error UsersResult = HashSet_Deconstruct(&Self->_activeUsers);
        Error_Deconstruct(&UsersResult);
        Error LoadedResult = HashMap_Deconstruct(&Self->_loadedAssets);
        Error_Deconstruct(&LoadedResult);
        Error DefinitionsResult = HashMap_Deconstruct(&Self->_definitions);
        Error_Deconstruct(&DefinitionsResult);
        ArrayList_Deconstruct(&Self->_types);
        ArrayList_Deconstruct(&Self->_searchRoots);
        ArrayList_Deconstruct(&Self->_referenceBlobs);
        Memory_Free(Self);
        return Result;
    }

    Self->_nextUserID = 1U;
    Self->_nextTypeID = 1U;
    Self->_tempFileCounter = 0U;
    Self->_cacheDirectory = DuplicateString(ASSET_MANAGER_DEFAULT_CACHE_DIRECTORY);
    Self->_logger = logger;
    Self->_lastReadStamp = DEFINITION_READ_STAMP_CODE;
    Self->_loadFailureCount = 0U;

    // A cache directory that cannot be prepared only matters once a reference resource needs a temp file.
    Error CacheResult = EnsureCacheDirectory(Self);
    if (CacheResult.Code == ErrorCode_Success)
    {
        CacheResult = ClearCacheDirectory(Self);
    }
    if (CacheResult.Code != ErrorCode_Success)
    {
        if (logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(logger, u8"Could not prepare the asset cache directory \"%s\": %s",
                Self->_cacheDirectory, GetErrorMessage(&CacheResult));
            Error_Deconstruct(&LogResult);
        }
        Error_Deconstruct(&CacheResult);
    }

    *outSelf = Self;
    return Error_CreateSuccess();
}

Error AssetManager_Deconstruct(AssetManager* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error FirstError = Error_CreateSuccess();

    // Unload every asset regardless of remaining holders.
    while (IMap_GetEntryCount(LoadedMap(self)) > 0U)
    {
        LoadedAssetRecord* Record = NULL;
        Error FindResult = FindRecord(self, true, &Record);
        if (FindResult.Code != ErrorCode_Success)
        {
            KeepFirstError(&FirstError, &FindResult);
            break;
        }
        if (Record == NULL)
        {
            break;
        }
        Error UnloadResult = UnloadRecord(self, Record);
        KeepFirstError(&FirstError, &UnloadResult);
    }

    // Release every definition record (releasing does not mutate the map).
    ICollection* DefinitionValues = IMap_AsValueCollection(DefinitionsMap(self));
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(DefinitionValues);
    if (Enumerator != NULL)
    {
        while (true)
        {
            bool HasNext = false;
            Error NextCheck = CollectionEnumerator_HasNext(Enumerator, &HasNext);
            if (NextCheck.Code != ErrorCode_Success) { KeepFirstError(&FirstError, &NextCheck); break; }
            if (!HasNext) { break; }
            DefinitionRecord Record;
            Error NextResult = CollectionEnumerator_NextByValue(Enumerator, &Record);
            if (NextResult.Code != ErrorCode_Success) { KeepFirstError(&FirstError, &NextResult); break; }
            ReleaseDefinitionRecord(self, &Record);
        }
        CollectionEnumerator_Destroy(Enumerator);
    }

    // Free types.
    size_t TypeCount = IList_GetElementCount(TypesList(self));
    for (size_t i = 0; i < TypeCount; i++)
    {
        TypeRecord* Record = NULL;
        Error GetResult = IList_GetElement(TypesList(self), i, &Record);
        if (GetResult.Code != ErrorCode_Success) { KeepFirstError(&FirstError, &GetResult); continue; }
        FreeTypeRecord(Record);
    }

    // Free search roots.
    size_t RootCount = IList_GetElementCount(RootsList(self));
    for (size_t i = 0; i < RootCount; i++)
    {
        unsigned char* Root = NULL;
        Error GetResult = IList_GetElement(RootsList(self), i, &Root);
        if (GetResult.Code != ErrorCode_Success) { KeepFirstError(&FirstError, &GetResult); continue; }
        Memory_Free(Root);
    }

    // Free reference blobs.
    size_t ReferenceCount = IList_GetElementCount(ReferenceList(self));
    for (size_t i = 0; i < ReferenceCount; i++)
    {
        ReferenceBlob* Blob = NULL;
        Error GetResult = IList_GetElement(ReferenceList(self), i, &Blob);
        if (GetResult.Code != ErrorCode_Success) { KeepFirstError(&FirstError, &GetResult); continue; }
        Memory_Free(Blob->Name);
        Memory_Free(Blob->Bytes);
        Memory_Free(Blob);
    }

    Error ClearResult = ClearCacheDirectory(self);
    KeepFirstError(&FirstError, &ClearResult);

    Error DefinitionsResult = HashMap_Deconstruct(&self->_definitions);
    KeepFirstError(&FirstError, &DefinitionsResult);
    Error LoadedResult = HashMap_Deconstruct(&self->_loadedAssets);
    KeepFirstError(&FirstError, &LoadedResult);
    Error UsersResult = HashSet_Deconstruct(&self->_activeUsers);
    KeepFirstError(&FirstError, &UsersResult);
    ArrayList_Deconstruct(&self->_types);
    ArrayList_Deconstruct(&self->_searchRoots);
    ArrayList_Deconstruct(&self->_referenceBlobs);
    Error PoolResult = BufferPool_Deconstruct(&self->_bufferPool);
    KeepFirstError(&FirstError, &PoolResult);
    Memory_Free(self->_cacheDirectory);
    Memory_Free(self);
    return FirstError;
}

Error AssetManager_CreateAssetType(AssetManager* self, const AssetTypeInfo* info, AssetTypeID* outID)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (info == NULL) { return CreateNullError(u8"info"); }
    if (outID == NULL) { return CreateNullError(u8"outID"); }
    *outID = ASSET_TYPE_ID_INVALID;

    if (StringUTF8_IsNullOrEmpty(info->Name)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Type name is empty."); }
    if (StringUTF8_IsNullOrEmpty(info->DirectoryName)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Type directory name is empty."); }
    if (info->Constructor == NULL) { return CreateNullError(u8"info->Constructor"); }

    if (FindTypeByNameOrDirectory(self, info->Name, NULL) != NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"An asset type named \"%s\" already exists.", info->Name);
    }
    if (FindTypeByNameOrDirectory(self, NULL, info->DirectoryName) != NULL)
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"An asset type with directory \"%s\" already exists.", info->DirectoryName);
    }

    Error ClearResult = ClearAllLoadFailures(self);
    if (ClearResult.Code != ErrorCode_Success) { return ClearResult; }

    TypeRecord* Record = Memory_Allocate(sizeof(TypeRecord));
    Memory_Zero(Record, sizeof(*Record));
    Record->ID = self->_nextTypeID;
    Record->Name = DuplicateString(info->Name);
    Record->DirectoryName = DuplicateString(info->DirectoryName);
    Record->DefinitionFileExtension = DuplicateString(info->DefinitionFileExtension);
    Record->Constructor = info->Constructor;
    Record->ConstructorUserData = info->ConstructorUserData;

    Error AddResult = IList_AddLast(TypesList(self), &Record);
    if (AddResult.Code != ErrorCode_Success)
    {
        FreeTypeRecord(Record);
        return AddResult;
    }

    self->_nextTypeID++;
    *outID = Record->ID;
    return Error_CreateSuccess();
}

Error AssetManager_CreateStandardAssetTypes(AssetManager* self, StandardAssetTypes* outTypes, JSONObjectPool** outDefinitionPool)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outTypes == NULL) { return CreateNullError(u8"outTypes"); }
    if (outDefinitionPool == NULL) { return CreateNullError(u8"outDefinitionPool"); }
    *outDefinitionPool = NULL;
    Memory_Zero(outTypes, sizeof(*outTypes));

    JSONObjectPool* Pool = NULL;
    Error PoolResult = JSONObjectPool_Create(&Pool);
    if (PoolResult.Code != ErrorCode_Success)
    {
        return PoolResult;
    }

    UserData PoolUserData = UserData_FromPointer(Pool);
    struct
    {
        const unsigned char* Name;
        const unsigned char* Directory;
        AssetDefinitionConstructor Constructor;
        AssetTypeID* Out;
    }
    Standard[] =
    {
        { ASSET_TYPE_NAME_SPRITE_SHEET, ASSET_TYPE_DIRECTORY_SPRITE_SHEET, SpriteSheetDefinition_Construct, &outTypes->SpriteSheet },
        { ASSET_TYPE_NAME_SPRITE_ANIMATION, ASSET_TYPE_DIRECTORY_SPRITE_ANIMATION, SpriteAnimationDefinition_Construct, &outTypes->SpriteAnimation },
        { ASSET_TYPE_NAME_SOUND, ASSET_TYPE_DIRECTORY_SOUND, SoundDefinition_Construct, &outTypes->Sound },
        { ASSET_TYPE_NAME_FONT, ASSET_TYPE_DIRECTORY_FONT, FontDefinition_Construct, &outTypes->Font },
        { ASSET_TYPE_NAME_SHADER, ASSET_TYPE_DIRECTORY_SHADER, ShaderDefinition_Construct, &outTypes->Shader },
        { ASSET_TYPE_NAME_MODEL, ASSET_TYPE_DIRECTORY_MODEL, ModelDefinition_Construct, &outTypes->Model },
    };

    for (size_t i = 0; i < (sizeof(Standard) / sizeof(Standard[0])); i++)
    {
        AssetTypeInfo Info;
        Memory_Zero(&Info, sizeof(Info));
        Info.Name = Standard[i].Name;
        Info.DirectoryName = Standard[i].Directory;
        Info.DefinitionFileExtension = ASSET_TYPE_DEFINITION_EXTENSION;
        Info.Constructor = Standard[i].Constructor;
        Info.ConstructorUserData = PoolUserData;

        AssetTypeID ID = ASSET_TYPE_ID_INVALID;
        Error CreateResult = AssetManager_CreateAssetType(self, &Info, &ID);
        if (CreateResult.Code != ErrorCode_Success)
        {
            Error DeconstructResult = JSONObjectPool_Deconstruct(Pool);
            Error_Deconstruct(&DeconstructResult);
            return CreateResult;
        }
        *Standard[i].Out = ID;
    }

    self->_standardTypes = *outTypes;
    *outDefinitionPool = Pool;
    return Error_CreateSuccess();
}

Error AssetManager_RemoveAssetType(AssetManager* self, AssetTypeID id)
{
    if (self == NULL) { return CreateNullError(u8"self"); }

    IList* List = TypesList(self);
    size_t Count = IList_GetElementCount(List);
    for (size_t i = 0; i < Count; i++)
    {
        TypeRecord* Record = NULL;
        Error GetResult = IList_GetElement(List, i, &Record);
        if (GetResult.Code != ErrorCode_Success) { return GetResult; }
        if (Record->ID != id) { continue; }

        // Refuse while any definition or loaded asset of the type remains.
        bool HasDefinitions = false;
        Error DefinitionsResult = MapHasKeyOfType(DefinitionsMap(self), id, &HasDefinitions);
        if (DefinitionsResult.Code != ErrorCode_Success) { return DefinitionsResult; }
        bool HasLoadedAssets = false;
        Error LoadedResult = MapHasKeyOfType(LoadedMap(self), id, &HasLoadedAssets);
        if (LoadedResult.Code != ErrorCode_Success) { return LoadedResult; }
        if (HasDefinitions || HasLoadedAssets)
        {
            return Error_Construct3(ErrorCode_InvalidOperation,
                u8"Cannot remove asset type \"%s\" while definitions or loaded assets of it remain.", Record->Name);
        }

        Error ClearResult = ClearAllLoadFailures(self);
        if (ClearResult.Code != ErrorCode_Success) { return ClearResult; }

        Error RemoveResult = IList_RemoveAt(List, i);
        if (RemoveResult.Code != ErrorCode_Success) { return RemoveResult; }
        FreeTypeRecord(Record);
        return Error_CreateSuccess();
    }
    return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id.");
}

Error AssetManager_GetAssetTypeDirectoryName(AssetManager* self, AssetTypeID id, const unsigned char** outName)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outName == NULL) { return CreateNullError(u8"outName"); }
    TypeRecord* Record = FindTypeByID(self, id);
    if (Record == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }
    *outName = Record->DirectoryName;
    return Error_CreateSuccess();
}

Error AssetManager_GetAssetTypeName(AssetManager* self, AssetTypeID id, const unsigned char** outName)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outName == NULL) { return CreateNullError(u8"outName"); }
    TypeRecord* Record = FindTypeByID(self, id);
    if (Record == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }
    *outName = Record->Name;
    return Error_CreateSuccess();
}

Error AssetManager_GetAssetTypeByName(AssetManager* self, const unsigned char* name, AssetTypeID* outID)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (name == NULL) { return CreateNullError(u8"name"); }
    if (outID == NULL) { return CreateNullError(u8"outID"); }
    TypeRecord* Record = FindTypeByNameOrDirectory(self, name, NULL);
    *outID = (Record != NULL) ? Record->ID : ASSET_TYPE_ID_INVALID;
    return Error_CreateSuccess();
}

Error AssetManager_AddSearchRoot(AssetManager* self, const unsigned char* directory)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (StringUTF8_IsNullOrEmpty(directory)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Directory is empty."); }

    Error ClearResult = ClearAllLoadFailures(self);
    if (ClearResult.Code != ErrorCode_Success) { return ClearResult; }

    unsigned char* Copy = DuplicateString(directory);
    Error AddResult = IList_AddLast(RootsList(self), &Copy);
    if (AddResult.Code != ErrorCode_Success)
    {
        Memory_Free(Copy);
        return AddResult;
    }
    return Error_CreateSuccess();
}

Error AssetManager_InsertSearchRoot(AssetManager* self, size_t index, const unsigned char* directory)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (StringUTF8_IsNullOrEmpty(directory)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Directory is empty."); }
    if (index > IList_GetElementCount(RootsList(self))) { return Error_Construct3(ErrorCode_IndexOutOfBounds, u8"Search root index out of range."); }

    Error ClearResult = ClearAllLoadFailures(self);
    if (ClearResult.Code != ErrorCode_Success) { return ClearResult; }

    unsigned char* Copy = DuplicateString(directory);
    Error InsertResult = IList_Insert(RootsList(self), index, &Copy);
    if (InsertResult.Code != ErrorCode_Success)
    {
        Memory_Free(Copy);
        return InsertResult;
    }
    return Error_CreateSuccess();
}

Error AssetManager_RemoveSearchRoot(AssetManager* self, const unsigned char* directory)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (directory == NULL) { return CreateNullError(u8"directory"); }

    IList* List = RootsList(self);
    size_t Count = IList_GetElementCount(List);
    for (size_t i = 0; i < Count; i++)
    {
        unsigned char* Root = NULL;
        Error GetResult = IList_GetElement(List, i, &Root);
        if (GetResult.Code != ErrorCode_Success) { return GetResult; }
        if (StringsEqual(Root, directory))
        {
            Error ClearResult = ClearAllLoadFailures(self);
            if (ClearResult.Code != ErrorCode_Success) { return ClearResult; }

            Error RemoveResult = IList_RemoveAt(List, i);
            if (RemoveResult.Code != ErrorCode_Success) { return RemoveResult; }
            Memory_Free(Root);
            return Error_CreateSuccess();
        }
    }
    return Error_Construct3(ErrorCode_InvalidOperation, u8"Search root not present.");
}

size_t AssetManager_GetSearchRootCount(AssetManager* self)
{
    if (self == NULL) { return 0U; }
    return IList_GetElementCount(RootsList(self));
}

Error AssetManager_GetSearchRoot(AssetManager* self, size_t index, const unsigned char** outDirectory)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outDirectory == NULL) { return CreateNullError(u8"outDirectory"); }
    if (index >= IList_GetElementCount(RootsList(self))) { return Error_Construct3(ErrorCode_IndexOutOfBounds, u8"Search root index out of range."); }

    unsigned char* Root = NULL;
    Error GetResult = IList_GetElement(RootsList(self), index, &Root);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }
    *outDirectory = Root;
    return Error_CreateSuccess();
}

Error AssetManager_SetReferenceResolver(AssetManager* self, AssetReferenceResolver resolver, const UserData* userData)
{
    if (self == NULL) { return CreateNullError(u8"self"); }

    Error ClearResult = ClearAllLoadFailures(self);
    if (ClearResult.Code != ErrorCode_Success) { return ClearResult; }

    self->_referenceResolver = resolver;
    self->_referenceResolverUserData = (userData != NULL) ? *userData : UserData_CreateEmpty();
    return Error_CreateSuccess();
}

Error AssetManager_SetReferenceBytes(AssetManager* self, const unsigned char* referenceName,
    const unsigned char* bytes, size_t byteCount)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (StringUTF8_IsNullOrEmpty(referenceName)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Reference name is empty."); }
    if ((bytes == NULL) && (byteCount > 0U)) { return CreateNullError(u8"bytes"); }

    Error ClearResult = ClearAllLoadFailures(self);
    if (ClearResult.Code != ErrorCode_Success) { return ClearResult; }

    unsigned char* BytesCopy = NULL;
    if (byteCount > 0U)
    {
        BytesCopy = Memory_Allocate(byteCount);
        Memory_Copy(bytes, BytesCopy, byteCount);
    }

    ReferenceBlob* Existing = FindReferenceBlob(self, referenceName);
    if (Existing != NULL)
    {
        Memory_Free(Existing->Bytes);
        Existing->Bytes = BytesCopy;
        Existing->Count = byteCount;
        return Error_CreateSuccess();
    }

    ReferenceBlob* Blob = Memory_Allocate(sizeof(ReferenceBlob));
    Memory_Zero(Blob, sizeof(*Blob));
    Blob->Name = DuplicateString(referenceName);
    Blob->Bytes = BytesCopy;
    Blob->Count = byteCount;

    Error AddResult = IList_AddLast(ReferenceList(self), &Blob);
    if (AddResult.Code != ErrorCode_Success)
    {
        Memory_Free(Blob->Name);
        Memory_Free(Blob->Bytes);
        Memory_Free(Blob);
        return AddResult;
    }
    return Error_CreateSuccess();
}

bool AssetExtensionFilter_Accepts(const AssetExtensionFilter* self, const unsigned char* extension)
{
    if ((self == NULL) || (self->Extensions == NULL) || (self->Count == 0U))
    {
        return true;
    }

    const unsigned char* Extension = (extension != NULL) ? extension : (const unsigned char*)u8"";
    for (size_t i = 0; i < self->Count; i++)
    {
        if ((self->Extensions[i] != NULL) && EqualsIgnoreAsciiCase(self->Extensions[i], Extension))
        {
            return true;
        }
    }
    return false;
}

Error AssetExtensionFilter_AppendList(const AssetExtensionFilter* self, GenericBuffer* destination)
{
    if (destination == NULL) { return CreateNullError(u8"destination"); }

    if ((self == NULL) || (self->Extensions == NULL) || (self->Count == 0U))
    {
        return StringUTF8_CopyTo(ANY_EXTENSION_TEXT, destination);
    }

    for (size_t i = 0; i < self->Count; i++)
    {
        if (i > 0U)
        {
            Error SeparatorResult = StringUTF8_CopyTo(NAME_LIST_SEPARATOR, destination);
            if (SeparatorResult.Code != ErrorCode_Success) { return SeparatorResult; }
        }
        const unsigned char* Extension = (self->Extensions[i] != NULL) ? self->Extensions[i] : (const unsigned char*)u8"";
        Error ExtensionResult = StringUTF8_CopyTo(Extension, destination);
        if (ExtensionResult.Code != ErrorCode_Success) { return ExtensionResult; }
    }
    return Error_CreateSuccess();
}

Error AssetManager_OpenResource(AssetManager* self, AssetTypeID assetType, const AssetLocation* location,
    const AssetExtensionFilter* extensions, IOStream** outStream)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (location == NULL) { return CreateNullError(u8"location"); }
    if (location->Value == NULL) { return CreateNullError(u8"location->Value"); }
    if (outStream == NULL) { return CreateNullError(u8"outStream"); }
    *outStream = NULL;

    TypeRecord* Type = FindTypeByID(self, assetType);
    if (Type == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }

    if (location->Type == AssetLocationType_File)
    {
        GenericBuffer FullPath;
        GenericBuffer_AllocateVariable(&FullPath, 0U, 1U);
        Error ResolveResult = ResolveExistingFilePath(self, Type, location->Value, extensions, &FullPath);
        if (ResolveResult.Code != ErrorCode_Success)
        {
            Memory_Free(FullPath._data);
            return ResolveResult;
        }

        OpenedResource* Resource = Memory_Allocate(sizeof(OpenedResource));
        Memory_Zero(Resource, sizeof(*Resource));
        Resource->IsFile = true;
        Error OpenResult = FileSystem_OpenFileStream(FullPath._data, FileOpenMode_ReadBinary, &Resource->Stream.File);
        Memory_Free(FullPath._data);
        if (OpenResult.Code != ErrorCode_Success)
        {
            Memory_Free(Resource);
            return OpenResult;
        }
        *outStream = FileStream_AsIOStream(&Resource->Stream.File);
        return Error_CreateSuccess();
    }

    OpenedResource* Resource = Memory_Allocate(sizeof(OpenedResource));
    Memory_Zero(Resource, sizeof(*Resource));
    Resource->IsFile = false;
    GenericBuffer_AllocateVariable(&Resource->BackingBytes, 0U, 1U);
    Resource->HasBacking = true;

    Error BytesResult = GetReferenceBytes(self, location->Value, &Resource->BackingBytes);
    if (BytesResult.Code != ErrorCode_Success)
    {
        Memory_Free(Resource->BackingBytes._data);
        Memory_Free(Resource);
        return BytesResult;
    }

    Error StreamResult = MemoryStream_Construct2(&Resource->Stream.Memory, &Resource->BackingBytes, IOStreamFlags_CanRead);
    if (StreamResult.Code != ErrorCode_Success)
    {
        Memory_Free(Resource->BackingBytes._data);
        Memory_Free(Resource);
        return StreamResult;
    }
    *outStream = MemoryStream_AsIOStream(&Resource->Stream.Memory);
    return Error_CreateSuccess();
}

Error AssetManager_CloseResource(AssetManager* self, IOStream* stream)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (stream == NULL) { return CreateNullError(u8"stream"); }

    // The active concrete stream is the first member of OpenedResource, so the IOStream* equals it.
    OpenedResource* Resource = (OpenedResource*)stream;
    Error Result = Error_CreateSuccess();
    if (Resource->IsFile)
    {
        Result = FileStream_Deconstruct(&Resource->Stream.File);
    }
    else
    {
        MemoryStream_Deconstruct(&Resource->Stream.Memory);
    }
    if (Resource->HasBacking)
    {
        Memory_Free(Resource->BackingBytes._data);
    }
    Memory_Free(Resource);
    return Result;
}

Error AssetManager_AcquireResourcePath(AssetManager* self, AssetTypeID assetType, const AssetLocation* location,
    const AssetExtensionFilter* extensions, const unsigned char* preferredExtension, AssetResourcePath** outHandle)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (location == NULL) { return CreateNullError(u8"location"); }
    if (location->Value == NULL) { return CreateNullError(u8"location->Value"); }
    if (outHandle == NULL) { return CreateNullError(u8"outHandle"); }
    *outHandle = NULL;

    TypeRecord* Type = FindTypeByID(self, assetType);
    if (Type == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }

    if (location->Type == AssetLocationType_File)
    {
        GenericBuffer FullPath;
        GenericBuffer_AllocateVariable(&FullPath, 0U, 1U);
        Error ResolveResult = ResolveExistingFilePath(self, Type, location->Value, extensions, &FullPath);
        if (ResolveResult.Code != ErrorCode_Success)
        {
            Memory_Free(FullPath._data);
            return ResolveResult;
        }

        AssetResourcePath* Handle = Memory_Allocate(sizeof(AssetResourcePath));
        Memory_Zero(Handle, sizeof(*Handle));
        Handle->Path = DuplicateString(FullPath._data);
        Handle->IsTemp = false;
        Memory_Free(FullPath._data);
        *outHandle = Handle;
        return Error_CreateSuccess();
    }

    if (preferredExtension == NULL) { return CreateNullError(u8"preferredExtension"); }

    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, 1U);
    Error BytesResult = GetReferenceBytes(self, location->Value, &Bytes);
    if (BytesResult.Code != ErrorCode_Success)
    {
        Memory_Free(Bytes._data);
        return BytesResult;
    }

    Error EnsureResult = EnsureCacheDirectory(self);
    if (EnsureResult.Code != ErrorCode_Success)
    {
        Memory_Free(Bytes._data);
        return EnsureResult;
    }

    GenericBuffer TempPath;
    GenericBuffer_AllocateVariable(&TempPath, 0U, 1U);
    Error PathResult = BuildTempFilePath(self, preferredExtension, &TempPath);
    if (PathResult.Code != ErrorCode_Success)
    {
        Memory_Free(Bytes._data);
        Memory_Free(TempPath._data);
        return PathResult;
    }

    Error WriteResult = FileSystem_WriteAllBytes(TempPath._data, Bytes._data, Bytes._count);
    Memory_Free(Bytes._data);
    if (WriteResult.Code != ErrorCode_Success)
    {
        Memory_Free(TempPath._data);
        return WriteResult;
    }

    AssetResourcePath* Handle = Memory_Allocate(sizeof(AssetResourcePath));
    Memory_Zero(Handle, sizeof(*Handle));
    Handle->Path = DuplicateString(TempPath._data);
    Handle->IsTemp = true;
    Memory_Free(TempPath._data);
    *outHandle = Handle;
    return Error_CreateSuccess();
}

const unsigned char* AssetResourcePath_Get(AssetResourcePath* self)
{
    if (self == NULL) { return NULL; }
    return self->Path;
}

Error AssetManager_ReleaseResourcePath(AssetManager* self, AssetResourcePath* handle)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (handle == NULL) { return CreateNullError(u8"handle"); }

    Error Result = Error_CreateSuccess();
    if (handle->IsTemp && (handle->Path != NULL))
    {
        Result = FileSystem_DeleteEntry(handle->Path);
    }
    Memory_Free(handle->Path);
    Memory_Free(handle);
    return Result;
}

Error AssetManager_SetCacheDirectory(AssetManager* self, const unsigned char* directory)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (StringUTF8_IsNullOrEmpty(directory)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Directory is empty."); }

    unsigned char* Copy = DuplicateString(directory);
    Memory_Free(self->_cacheDirectory);
    self->_cacheDirectory = Copy;

    Error EnsureResult = EnsureCacheDirectory(self);
    if (EnsureResult.Code != ErrorCode_Success)
    {
        return EnsureResult;
    }
    return ClearCacheDirectory(self);
}

Error AssetManager_ReadDefinitions(AssetManager* self, size_t* outFailedFileCount)
{
    if (outFailedFileCount != NULL) { *outFailedFileCount = 0U; }
    if (self == NULL) { return CreateNullError(u8"self"); }

    DefinitionScan Scan;
    GenericBuffer_AllocateVariable(&Scan.DirectoryPath, 0U, 1U);
    GenericBuffer_AllocateVariable(&Scan.FileBytes, 0U, 1U);
    GenericBuffer_AllocateVariable(&Scan.FileExtension, 0U, 1U);
    Scan.PassFirstStamp = self->_lastReadStamp + 1U;
    Scan.FailedFileCount = 0U;
    Scan.FirstError = ClearAllLoadFailures(self);

    // Roots in priority order (index 0 = highest), so the first root to register a name wins the override.
    size_t RootCount = IList_GetElementCount(RootsList(self));
    size_t TypeCount = IList_GetElementCount(TypesList(self));
    for (size_t rootIndex = 0; rootIndex < RootCount; rootIndex++)
    {
        self->_lastReadStamp++;
        uint64_t RootStamp = self->_lastReadStamp;

        unsigned char* Root = NULL;
        Error GetRootResult = IList_GetElement(RootsList(self), rootIndex, &Root);
        if (GetRootResult.Code != ErrorCode_Success)
        {
            KeepFirstError(&Scan.FirstError, &GetRootResult);
            continue;
        }

        for (size_t typeIndex = 0; typeIndex < TypeCount; typeIndex++)
        {
            TypeRecord* Type = NULL;
            Error GetTypeResult = IList_GetElement(TypesList(self), typeIndex, &Type);
            if (GetTypeResult.Code != ErrorCode_Success)
            {
                KeepFirstError(&Scan.FirstError, &GetTypeResult);
                continue;
            }
            ReadTypeDirectory(self, Root, Type, RootStamp, &Scan);
        }
    }

    if ((Scan.FailedFileCount > 0U) && (self->_logger != NULL))
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            u8"Skipped %" PRIu64 u8" asset definition file(s) while reading definitions; see the errors above.",
            (uint64_t)Scan.FailedFileCount);
        Error_Deconstruct(&LogResult);
    }

    if (outFailedFileCount != NULL) { *outFailedFileCount = Scan.FailedFileCount; }
    Memory_Free(Scan.DirectoryPath._data);
    Memory_Free(Scan.FileBytes._data);
    Memory_Free(Scan.FileExtension._data);
    return Scan.FirstError;
}

Error AssetManager_BuildDefinition(AssetManager* self, AssetTypeID type, const GenericBuffer* rawData,
    const unsigned char* sourceDescription, AssetDefinition** outDefinition)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (rawData == NULL) { return CreateNullError(u8"rawData"); }
    if (outDefinition == NULL) { return CreateNullError(u8"outDefinition"); }
    *outDefinition = NULL;

    TypeRecord* Type = FindTypeByID(self, type);
    if (Type == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }

    AssetDefinition* Definition = NULL;
    Error ConstructResult = Type->Constructor(self, &Type->ConstructorUserData, rawData, sourceDescription, &Definition);
    if (ConstructResult.Code != ErrorCode_Success) { return ConstructResult; }
    if (Definition == NULL) { return Error_Construct3(ErrorCode_InvalidAssetDefinition, u8"Constructor produced no definition."); }

    Definition->Type = type;
    *outDefinition = Definition;
    return Error_CreateSuccess();
}

Error AssetManager_SetDefinition(AssetManager* self, AssetDefinition* definition)
{
    if (definition == NULL) { return CreateNullError(u8"definition"); }
    if (!CanDestroyDefinition(definition))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Definition has no destroy function; ownership stays with the caller.");
    }
    if (self == NULL)
    {
        definition->VTable->Destroy(definition);
        return CreateNullError(u8"self");
    }

    Error ClearResult = ClearAllLoadFailures(self);
    if (ClearResult.Code != ErrorCode_Success)
    {
        definition->VTable->Destroy(definition);
        return ClearResult;
    }

    DefinitionSource Source = { NULL, DEFINITION_READ_STAMP_CODE, DEFINITION_READ_STAMP_CODE };
    return RegisterDefinition(self, definition, &Source);
}

Error AssetManager_RemoveDefinition(AssetManager* self, AssetTypeID type, const unsigned char* name)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (name == NULL) { return CreateNullError(u8"name"); }

    DefinitionRecord* Record = NULL;
    if (!TryGetDefinitionRecord(self, type, name, &Record))
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"No definition named \"%s\" for the type.", name);
    }

    Error ClearResult = ClearAllLoadFailures(self);
    if (ClearResult.Code != ErrorCode_Success) { return ClearResult; }
    return RemoveDefinitionRecord(self, type, name);
}

Error AssetManager_HasDefinition(AssetManager* self, AssetTypeID type, const unsigned char* name, bool* outExists)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (name == NULL) { return CreateNullError(u8"name"); }
    if (outExists == NULL) { return CreateNullError(u8"outExists"); }
    DefinitionRecord* Record = NULL;
    *outExists = TryGetDefinitionRecord(self, type, name, &Record);
    return Error_CreateSuccess();
}

bool AssetManager_IsValidAssetName(const unsigned char* name)
{
    // Every allowed character is ASCII, so a byte-wise check is exact for UTF-8: any byte of a multi-byte
    // sequence is >= 0x80 and therefore rejected.
    if ((name == NULL) || !IsAsciiLowercaseLetter(name[0]))
    {
        return false;
    }
    for (const unsigned char* Cursor = name + 1; *Cursor != (unsigned char)u8'\0'; Cursor++)
    {
        unsigned char Character = *Cursor;
        bool IsAllowed = IsAsciiLowercaseLetter(Character)
            || IsAsciiDigit(Character)
            || (Character == (unsigned char)u8'_')
            || (Character == (unsigned char)u8'.')
            || (Character == (unsigned char)u8'-');
        if (!IsAllowed)
        {
            return false;
        }
    }
    return true;
}

Error AssetManager_ClearLoadFailures(AssetManager* self)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    return ClearAllLoadFailures(self);
}

Error AssetManager_GetNewUserID(AssetManager* self, AssetUserID* outID)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outID == NULL) { return CreateNullError(u8"outID"); }
    return MintUser(self, outID);
}

Error AssetManager_ReleaseAsset(AssetManager* self, AssetTypeID assetType, const unsigned char* name, AssetUserID user)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (name == NULL) { return CreateNullError(u8"name"); }

    LoadedAssetRecord* Record = NULL;
    if (!TryGetLoadedRecord(self, assetType, name, &Record))
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"Asset \"%s\" is not loaded.", name);
    }

    if (!UserSetContains(&Record->Holders, user))
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"User does not hold asset \"%s\".", name);
    }

    bool WasRemoved = false;
    Error RemoveResult = UserSetRemove(&Record->Holders, user, &WasRemoved);
    if (RemoveResult.Code != ErrorCode_Success) { return RemoveResult; }

    if (UserSetCount(&Record->Holders) == 0U)
    {
        return UnloadRecord(self, Record);
    }
    return Error_CreateSuccess();
}

Error AssetManager_ReleaseAllAssetsForUser(AssetManager* self, AssetUserID user)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (!IsUserActive(self, user)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Invalid asset user id."); }
    return ReleaseAllForUser(self, user);
}

Error AssetManager_RetireUser(AssetManager* self, AssetUserID user)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (!IsUserActive(self, user)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Invalid asset user id."); }

    Error ReleaseResult = ReleaseAllForUser(self, user);
    if (ReleaseResult.Code != ErrorCode_Success) { return ReleaseResult; }
    RemoveActiveUser(self, user);
    return Error_CreateSuccess();
}

Error AssetManager_LoadAssetSingle(AssetManager* self, AssetTypeID assetType, const unsigned char* name,
    AssetUserID user, void** outAsset)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (name == NULL) { return CreateNullError(u8"name"); }
    if (outAsset == NULL) { return CreateNullError(u8"outAsset"); }
    *outAsset = NULL;

    TypeRecord* Type = FindTypeByID(self, assetType);
    if (Type == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }
    if (!IsUserActive(self, user)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Invalid asset user id."); }

    // Already loaded: share it and record the hold.
    LoadedAssetRecord* Existing = NULL;
    if (TryGetLoadedRecord(self, assetType, name, &Existing))
    {
        bool WasAdded = false;
        Error AddResult = UserSetAdd(&Existing->Holders, user, &WasAdded);
        if (AddResult.Code != ErrorCode_Success) { return AddResult; }
        *outAsset = Existing->Loaded.Asset;
        return Error_CreateSuccess();
    }

    DefinitionRecord* DefinitionEntry = NULL;
    if (!TryGetDefinitionRecord(self, assetType, name, &DefinitionEntry))
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"No definition named \"%s\" for the type.", name);
    }

    // A remembered failure is returned at once: no I/O and no dependency user.
    if (DefinitionEntry->LoadFailure.Code != ErrorCode_Success)
    {
        return Error_Construct3(DefinitionEntry->LoadFailure.Code,
            u8"Asset \"%s\" failed to load earlier (not retried until definitions or roots change): %s",
            name, GetErrorMessage(&DefinitionEntry->LoadFailure));
    }

    // The record may move while loading (see DefinitionRecord); the definition object itself does not.
    AssetDefinition* Definition = DefinitionEntry->Definition;
    DefinitionEntry = NULL;

    AssetUserID DependencyUser = ASSET_USER_ID_INVALID;
    Error MintResult = MintUser(self, &DependencyUser);
    if (MintResult.Code != ErrorCode_Success) { return MintResult; }

    LoadedAsset Loaded;
    Memory_Zero(&Loaded, sizeof(Loaded));
    Error LoadResult = Definition->VTable->LoadAsset(Definition, self, DependencyUser, &Loaded);
    if (LoadResult.Code != ErrorCode_Success)
    {
        Error ReleaseResult = ReleaseAllForUser(self, DependencyUser);
        Error_Deconstruct(&ReleaseResult);
        RemoveActiveUser(self, DependencyUser);
        CacheLoadFailure(self, Type, name, &LoadResult);
        return LoadResult;
    }

    LoadedAssetRecord* Record = Memory_Allocate(sizeof(LoadedAssetRecord));
    Memory_Zero(Record, sizeof(*Record));
    Record->Type = assetType;
    Record->Name = DuplicateString(name);
    Record->Loaded = Loaded;
    Record->DependencyUser = DependencyUser;

    Error HoldersResult = ConstructUserSet(&Record->Holders);
    if (HoldersResult.Code != ErrorCode_Success)
    {
        if ((Loaded.VTable != NULL) && (Loaded.VTable->Destroy != NULL)) { Loaded.VTable->Destroy(&Record->Loaded, self); }
        Error ReleaseResult = ReleaseAllForUser(self, DependencyUser);
        Error_Deconstruct(&ReleaseResult);
        RemoveActiveUser(self, DependencyUser);
        Memory_Free(Record->Name);
        Memory_Free(Record);
        return HoldersResult;
    }

    bool WasAdded = false;
    Error UserAddResult = UserSetAdd(&Record->Holders, user, &WasAdded);
    if (UserAddResult.Code != ErrorCode_Success)
    {
        if ((Loaded.VTable != NULL) && (Loaded.VTable->Destroy != NULL)) { Loaded.VTable->Destroy(&Record->Loaded, self); }
        Error ReleaseResult = ReleaseAllForUser(self, DependencyUser);
        Error_Deconstruct(&ReleaseResult);
        RemoveActiveUser(self, DependencyUser);
        Error DeconstructResult = HashSet_Deconstruct(&Record->Holders);
        Error_Deconstruct(&DeconstructResult);
        Memory_Free(Record->Name);
        Memory_Free(Record);
        return UserAddResult;
    }

    AssetKey Key = { assetType, Record->Name };
    bool MapAdded = false;
    Error MapResult = IMap_Add(LoadedMap(self), &Key, &Record, &MapAdded);
    if (MapResult.Code != ErrorCode_Success)
    {
        if ((Loaded.VTable != NULL) && (Loaded.VTable->Destroy != NULL)) { Loaded.VTable->Destroy(&Record->Loaded, self); }
        Error ReleaseResult = ReleaseAllForUser(self, DependencyUser);
        Error_Deconstruct(&ReleaseResult);
        RemoveActiveUser(self, DependencyUser);
        Error DeconstructResult = HashSet_Deconstruct(&Record->Holders);
        Error_Deconstruct(&DeconstructResult);
        Memory_Free(Record->Name);
        Memory_Free(Record);
        return MapResult;
    }

    *outAsset = Record->Loaded.Asset;
    return Error_CreateSuccess();
}

/* Shared body of the typed convenience loaders. */
static Error LoadStandard(AssetManager* self, AssetTypeID standardType, const unsigned char* name,
    AssetUserID user, void** outAsset)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outAsset != NULL) { *outAsset = NULL; }
    if (standardType == ASSET_TYPE_ID_INVALID)
    {
        return Error_Construct3(ErrorCode_InvalidState, u8"Standard asset types were not registered.");
    }
    return AssetManager_LoadAssetSingle(self, standardType, name, user, outAsset);
}

Error AssetManager_LoadSpriteSheet(AssetManager* self, const unsigned char* name, AssetUserID user, SpriteSheet** outAsset)
{
    return LoadStandard(self, (self != NULL) ? self->_standardTypes.SpriteSheet : ASSET_TYPE_ID_INVALID, name, user, (void**)outAsset);
}

Error AssetManager_LoadSpriteAnimation(AssetManager* self, const unsigned char* name, AssetUserID user, SpriteAnimation** outAsset)
{
    return LoadStandard(self, (self != NULL) ? self->_standardTypes.SpriteAnimation : ASSET_TYPE_ID_INVALID, name, user, (void**)outAsset);
}

Error AssetManager_LoadSound(AssetManager* self, const unsigned char* name, AssetUserID user, GameSound** outAsset)
{
    return LoadStandard(self, (self != NULL) ? self->_standardTypes.Sound : ASSET_TYPE_ID_INVALID, name, user, (void**)outAsset);
}

Error AssetManager_LoadFont(AssetManager* self, const unsigned char* name, AssetUserID user, GameFont** outAsset)
{
    return LoadStandard(self, (self != NULL) ? self->_standardTypes.Font : ASSET_TYPE_ID_INVALID, name, user, (void**)outAsset);
}

Error AssetManager_LoadShader(AssetManager* self, const unsigned char* name, AssetUserID user, GameShader** outAsset)
{
    return LoadStandard(self, (self != NULL) ? self->_standardTypes.Shader : ASSET_TYPE_ID_INVALID, name, user, (void**)outAsset);
}

Error AssetManager_LoadModel(AssetManager* self, const unsigned char* name, AssetUserID user, GameModel** outAsset)
{
    return LoadStandard(self, (self != NULL) ? self->_standardTypes.Model : ASSET_TYPE_ID_INVALID, name, user, (void**)outAsset);
}

Error AssetManager_CreateAssetBulkOperation(AssetManager* self, AssetUserID user, AssetBulkOperation** outOp)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outOp == NULL) { return CreateNullError(u8"outOp"); }
    *outOp = NULL;
    if (!IsUserActive(self, user)) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Invalid asset user id."); }

    AssetBulkOperation* Operation = Memory_Allocate(sizeof(AssetBulkOperation));
    Memory_Zero(Operation, sizeof(*Operation));
    Operation->Manager = self;
    Operation->User = user;
    Operation->NextIndex = 0U;
    Operation->Progress.Total = 0U;
    Operation->Progress.Processed = 0U;
    ArrayList_Construct1(&Operation->Entries, sizeof(PromisedAsset*));

    *outOp = Operation;
    return Error_CreateSuccess();
}

Error AssetBulkOperation_AddEntry(AssetBulkOperation* self, AssetTypeID assetType, const unsigned char* name, PromisedAsset** outPromise)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (name == NULL) { return CreateNullError(u8"name"); }
    if (outPromise != NULL) { *outPromise = NULL; }
    if (self->NextIndex > 0U) { return Error_Construct3(ErrorCode_InvalidOperation, u8"Cannot add entries after stepping has begun."); }

    PromisedAsset* Entry = Memory_Allocate(sizeof(PromisedAsset));
    Memory_Zero(Entry, sizeof(*Entry));
    Entry->Type = assetType;
    Entry->Name = DuplicateString(name);
    Entry->Asset = NULL;
    Entry->Resolved = false;
    Entry->LoadError = Error_CreateSuccess();

    Error AddResult = IList_AddLast(&self->Entries._list, &Entry);
    if (AddResult.Code != ErrorCode_Success)
    {
        Memory_Free(Entry->Name);
        Memory_Free(Entry);
        return AddResult;
    }

    self->Progress.Total++;
    if (outPromise != NULL) { *outPromise = Entry; }
    return Error_CreateSuccess();
}

Error AssetBulkOperation_CompleteStep(AssetBulkOperation* self, bool* outDidWork)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outDidWork != NULL) { *outDidWork = false; }

    size_t Count = IList_GetElementCount(&self->Entries._list);
    if (self->NextIndex >= Count)
    {
        return Error_CreateSuccess();
    }

    PromisedAsset* Entry = NULL;
    Error GetResult = IList_GetElement(&self->Entries._list, self->NextIndex, &Entry);
    if (GetResult.Code != ErrorCode_Success) { return GetResult; }

    void* Asset = NULL;
    Error LoadResult = AssetManager_LoadAssetSingle(self->Manager, Entry->Type, Entry->Name, self->User, &Asset);
    Entry->Asset = Asset;
    Entry->Resolved = true;
    // Per the design, a failed asset load does not fail the step; it lives on the promise.
    Error_Deconstruct(&Entry->LoadError);
    Entry->LoadError = LoadResult;

    self->NextIndex++;
    self->Progress.Processed++;
    if (outDidWork != NULL) { *outDidWork = true; }
    return Error_CreateSuccess();
}

bool AssetBulkOperation_IsComplete(AssetBulkOperation* self)
{
    if (self == NULL) { return true; }
    return (self->NextIndex >= IList_GetElementCount(&self->Entries._list));
}

Error AssetBulkOperation_Deconstruct(AssetBulkOperation* self)
{
    if (self == NULL) { return Error_CreateSuccess(); }

    size_t Count = IList_GetElementCount(&self->Entries._list);
    for (size_t i = 0; i < Count; i++)
    {
        PromisedAsset* Entry = NULL;
        Error GetResult = IList_GetElement(&self->Entries._list, i, &Entry);
        if (GetResult.Code != ErrorCode_Success) { Error_Deconstruct(&GetResult); continue; }
        Error_Deconstruct(&Entry->LoadError);
        Memory_Free(Entry->Name);
        Memory_Free(Entry);
    }
    ArrayList_Deconstruct(&self->Entries);
    Memory_Free(self);
    return Error_CreateSuccess();
}

AssetLoadProgress* AssetBulkOperation_GetProgress(AssetBulkOperation* self)
{
    if (self == NULL) { return NULL; }
    return &self->Progress;
}

Error AssetLoadProgress_GetProgressFactor(AssetLoadProgress* self, double* outFactor)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outFactor == NULL) { return CreateNullError(u8"outFactor"); }
    if (self->Total == 0U) { *outFactor = 1.0; return Error_CreateSuccess(); }
    *outFactor = (double)self->Processed / (double)self->Total;
    return Error_CreateSuccess();
}

Error AssetLoadProgress_GetItemCountTotal(AssetLoadProgress* self, size_t* outItemCountTotal)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outItemCountTotal == NULL) { return CreateNullError(u8"outItemCountTotal"); }
    *outItemCountTotal = self->Total;
    return Error_CreateSuccess();
}

Error AssetLoadProgress_GetItemCountProcessed(AssetLoadProgress* self, size_t* outItemCountProcessed)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outItemCountProcessed == NULL) { return CreateNullError(u8"outItemCountProcessed"); }
    *outItemCountProcessed = self->Processed;
    return Error_CreateSuccess();
}

bool PromisedAsset_IsResolved(PromisedAsset* self)
{
    return (self != NULL) && self->Resolved;
}

Error PromisedAsset_GetAsset(PromisedAsset* self, void** outAsset)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outAsset == NULL) { return CreateNullError(u8"outAsset"); }
    *outAsset = NULL;
    if (!self->Resolved) { return Error_Construct3(ErrorCode_InvalidState, u8"Promise is not yet resolved."); }
    if (self->LoadError.Code != ErrorCode_Success) { return Error_Construct3(ErrorCode_InvalidOperation, u8"The asset failed to load."); }
    *outAsset = self->Asset;
    return Error_CreateSuccess();
}

Error PromisedAsset_GetError(PromisedAsset* self, Error* outError)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outError == NULL) { return CreateNullError(u8"outError"); }
    if (!self->Resolved) { return Error_Construct3(ErrorCode_InvalidState, u8"Promise is not yet resolved."); }

    if (self->LoadError.Code == ErrorCode_Success)
    {
        *outError = Error_CreateSuccess();
    }
    else
    {
        // Copy so the caller-owned Error's message is independent of the promise's.
        *outError = Error_Construct1(self->LoadError.Code, self->LoadError.Message);
    }
    return Error_CreateSuccess();
}

Error AssetManager_BorrowGenericBuffer(AssetManager* self, size_t elementSize, GenericBuffer** outBuffer)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (outBuffer == NULL) { return CreateNullError(u8"outBuffer"); }
    *outBuffer = NULL;
    if (elementSize == 0U) { return Error_Construct3(ErrorCode_ArgumentOutOfRange, u8"Element size must be greater than zero."); }
    return BufferPool_Borrow(&self->_bufferPool, elementSize, outBuffer);
}

Error AssetManager_ReturnGenericBuffer(AssetManager* self, GenericBuffer* buffer)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (buffer == NULL) { return CreateNullError(u8"buffer"); }
    return BufferPool_Return(&self->_bufferPool, buffer);
}
