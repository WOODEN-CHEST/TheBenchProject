#include "AssetManager.h"
#include "AssetTypesStandard.h"
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
    unsigned char* Name;          // owned
    unsigned char* DirectoryName; // owned
    AssetDefinitionConstructor Constructor;
    UserData ConstructorUserData;
} TypeRecord;

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
    HashMap _definitions;        // AssetKey -> AssetDefinition*
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
};


// Static functions.
static Error CreateNullError(const unsigned char* parameterName)
{
    return Error_Construct3(ErrorCode_IllegalArgument, u8"Parameter \"%s\" cannot be null.", parameterName);
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

static bool TryGetDefinition(AssetManager* self, AssetTypeID type, const unsigned char* name, AssetDefinition** outDef)
{
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
    Error GetResult = IMap_GetElement(DefinitionsMap(self), &Key, outDef);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return false;
    }
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

/* Registers @p definition, taking ownership. When @p allowOverride is false and a definition with the
   same (type, name) already exists, the new definition is destroyed and the existing one kept (used by
   ReadDefinitions where higher-priority roots are processed first). */
static Error SetDefinitionInternal(AssetManager* self, AssetDefinition* definition, bool allowOverride)
{
    if (FindTypeByID(self, definition->Type) == NULL)
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Definition has an unregistered type id.");
    }
    if (StringUTF8_IsNullOrEmpty(definition->Name))
    {
        return Error_Construct3(ErrorCode_IllegalArgument, u8"Definition has an empty name.");
    }

    AssetDefinition* Existing = NULL;
    bool Exists = TryGetDefinition(self, definition->Type, definition->Name, &Existing);
    if (Exists)
    {
        if (!allowOverride)
        {
            // Higher-priority definition already registered; discard the new one.
            definition->VTable->Destroy(definition);
            return Error_CreateSuccess();
        }

        AssetKey ExistingKey = { Existing->Type, Existing->Name };
        bool WasRemoved = false;
        Error RemoveResult = IMap_Remove(DefinitionsMap(self), &ExistingKey, &WasRemoved);
        if (RemoveResult.Code != ErrorCode_Success)
        {
            return RemoveResult;
        }
        Existing->VTable->Destroy(Existing);
    }

    AssetKey Key = { definition->Type, definition->Name };
    bool WasAdded = false;
    Error AddResult = IMap_Add(DefinitionsMap(self), &Key, &definition, &WasAdded);
    if (AddResult.Code != ErrorCode_Success)
    {
        definition->VTable->Destroy(definition);
        return AddResult;
    }
    return Error_CreateSuccess();
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
            if (Result.Code == ErrorCode_Success) { Result = NextCheck; } else { Error_Deconstruct(&NextCheck); }
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
            if (Result.Code == ErrorCode_Success) { Result = NextResult; } else { Error_Deconstruct(&NextResult); }
            break;
        }

        Error DeleteResult = FileSystem_DeleteEntry(Info._path);
        if (DeleteResult.Code != ErrorCode_Success)
        {
            if (Result.Code == ErrorCode_Success) { Result = DeleteResult; } else { Error_Deconstruct(&DeleteResult); }
        }
        FileSystemEntryInfo_Deconstruct(&Info);
    }

    Error DeconstructResult = DirectoryEntryEnumerator_Deconstruct(Enumerator);
    if ((Result.Code == ErrorCode_Success) && (DeconstructResult.Code != ErrorCode_Success))
    {
        return DeconstructResult;
    }
    Error_Deconstruct(&DeconstructResult);
    return Result;
}

/* Resolves an extension-less relative path to an existing file across the search roots (priority order).
   Matches by comparing each candidate directory's file stems to the target stem, so the extension is
   discovered automatically. On success @p outFullPath receives the full path and @p outFound is true. */
static Error ResolveExistingFilePath(AssetManager* self, TypeRecord* type, const unsigned char* relativePath,
    GenericBuffer* outFullPath, bool* outFound)
{
    *outFound = false;

    const unsigned char* Relative = relativePath;
    if ((Relative[0] == (unsigned char)'/') || (Relative[0] == (unsigned char)'\\'))
    {
        Relative++;
    }

    GenericBuffer Combined;
    GenericBuffer Normalized;
    GenericBuffer ParentDirectory;
    GenericBuffer TargetStem;
    GenericBuffer FileStem;
    GenericBuffer_AllocateVariable(&Combined, 0U, 1U);
    GenericBuffer_AllocateVariable(&Normalized, 0U, 1U);
    GenericBuffer_AllocateVariable(&ParentDirectory, 0U, 1U);
    GenericBuffer_AllocateVariable(&TargetStem, 0U, 1U);
    GenericBuffer_AllocateVariable(&FileStem, 0U, 1U);

    Error Result = Error_CreateSuccess();
    size_t RootCount = IList_GetElementCount(RootsList(self));
    for (size_t i = 0; (i < RootCount) && !(*outFound); i++)
    {
        unsigned char* Root = NULL;
        Error GetRootResult = IList_GetElement(RootsList(self), i, &Root);
        if (GetRootResult.Code != ErrorCode_Success)
        {
            Result = GetRootResult;
            break;
        }

        GenericBuffer_Clear(&Combined);
        const unsigned char* Parts[3] = { Root, type->DirectoryName, Relative };
        Error CombineResult = Path_Combine(Parts, 3U, &Combined);
        if (CombineResult.Code != ErrorCode_Success)
        {
            Result = CombineResult;
            break;
        }

        GenericBuffer_Clear(&Normalized);
        Error NormalizeResult = Path_Normalize(Combined._data, PathNormalizeConditions_FixSeparators, &Normalized);
        if (NormalizeResult.Code != ErrorCode_Success)
        {
            Result = NormalizeResult;
            break;
        }

        GenericBuffer_Clear(&ParentDirectory);
        Error ParentResult = Path_GetParentPath(Normalized._data, &ParentDirectory);
        if (ParentResult.Code != ErrorCode_Success)
        {
            Result = ParentResult;
            break;
        }

        GenericBuffer_Clear(&TargetStem);
        Error StemResult = Path_GetLastEntryStem(Normalized._data, &TargetStem);
        if (StemResult.Code != ErrorCode_Success)
        {
            Result = StemResult;
            break;
        }

        DirectoryEntryEnumerator* Enumerator = NULL;
        Error FilesResult = FileSystem_GetFiles(ParentDirectory._data, DirectorySearchOption_TopLevel, &Enumerator);
        if ((FilesResult.Code == ErrorCode_DirectoryNotFound) || (FilesResult.Code == ErrorCode_FileNotFound))
        {
            Error_Deconstruct(&FilesResult);
            continue;
        }
        if (FilesResult.Code != ErrorCode_Success)
        {
            Result = FilesResult;
            break;
        }

        while (true)
        {
            bool HasNext = false;
            Error NextCheck = DirectoryEntryEnumerator_HasNext(Enumerator, &HasNext);
            if (NextCheck.Code != ErrorCode_Success)
            {
                Result = NextCheck;
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
                Result = NextResult;
                break;
            }

            GenericBuffer_Clear(&FileStem);
            Error FileStemResult = Path_GetLastEntryStem(Info._name, &FileStem);
            if (FileStemResult.Code != ErrorCode_Success)
            {
                Result = FileStemResult;
                FileSystemEntryInfo_Deconstruct(&Info);
                break;
            }

            if (StringsEqual(FileStem._data, TargetStem._data))
            {
                GenericBuffer_Clear(outFullPath);
                Error CopyResult = StringUTF8_CopyTo(Info._path, outFullPath);
                if (CopyResult.Code != ErrorCode_Success)
                {
                    Result = CopyResult;
                }
                else
                {
                    *outFound = true;
                }
                FileSystemEntryInfo_Deconstruct(&Info);
                break;
            }
            FileSystemEntryInfo_Deconstruct(&Info);
        }

        Error DeconstructResult = DirectoryEntryEnumerator_Deconstruct(Enumerator);
        if ((Result.Code == ErrorCode_Success) && (DeconstructResult.Code != ErrorCode_Success))
        {
            Result = DeconstructResult;
            break;
        }
        Error_Deconstruct(&DeconstructResult);
    }

    Memory_Free(Combined._data);
    Memory_Free(Normalized._data);
    Memory_Free(ParentDirectory._data);
    Memory_Free(TargetStem._data);
    Memory_Free(FileStem._data);
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
    Error Result = Error_CreateSuccess();

    if (!GenericBuffer_AppendString(&FileName, (const unsigned char*)u8"temp_"))
    {
        Result = Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to build temp file name.");
    }
    if (Result.Code == ErrorCode_Success)
    {
        Error NumberResult = Number_UInt64ToString(Number, NUMBER_BASE_10, false, &FileName);
        if (NumberResult.Code != ErrorCode_Success) { Result = NumberResult; }
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AppendString(&FileName, (const unsigned char*)u8"."))
    {
        Result = Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to build temp file name.");
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_AppendString(&FileName, extension))
    {
        Result = Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to build temp file name.");
    }
    if ((Result.Code == ErrorCode_Success) && !GenericBuffer_NullTerminate(&FileName))
    {
        Result = Error_Construct3(ErrorCode_BufferTooLarge, u8"Failed to build temp file name.");
    }

    if (Result.Code == ErrorCode_Success)
    {
        const unsigned char* Parts[2] = { self->_cacheDirectory, FileName._data };
        Result = Path_Combine(Parts, 2U, outPath);
    }

    Memory_Free(FileName._data);
    return Result;
}


// Public functions.
Error AssetManager_Construct1(AssetManager** outSelf)
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
        sizeof(AssetKey), sizeof(AssetDefinition*), HashAssetKey);
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
        HashMap_Deconstruct(&Self->_definitions);
        ArrayList_Deconstruct(&Self->_types);
        ArrayList_Deconstruct(&Self->_searchRoots);
        ArrayList_Deconstruct(&Self->_referenceBlobs);
        Memory_Free(Self);
        return Result;
    }

    Result = ConstructUserSet(&Self->_activeUsers);
    if (Result.Code != ErrorCode_Success)
    {
        HashMap_Deconstruct(&Self->_loadedAssets);
        HashMap_Deconstruct(&Self->_definitions);
        ArrayList_Deconstruct(&Self->_types);
        ArrayList_Deconstruct(&Self->_searchRoots);
        ArrayList_Deconstruct(&Self->_referenceBlobs);
        Memory_Free(Self);
        return Result;
    }

    Result = BufferPool_Construct1(&Self->_bufferPool);
    if (Result.Code != ErrorCode_Success)
    {
        HashSet_Deconstruct(&Self->_activeUsers);
        HashMap_Deconstruct(&Self->_loadedAssets);
        HashMap_Deconstruct(&Self->_definitions);
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

    Error CacheResult = EnsureCacheDirectory(Self);
    if (CacheResult.Code == ErrorCode_Success)
    {
        Error ClearResult = ClearCacheDirectory(Self);
        Error_Deconstruct(&ClearResult);
    }
    else
    {
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
            if (FirstError.Code == ErrorCode_Success) { FirstError = FindResult; } else { Error_Deconstruct(&FindResult); }
            break;
        }
        if (Record == NULL)
        {
            break;
        }
        Error UnloadResult = UnloadRecord(self, Record);
        if (UnloadResult.Code != ErrorCode_Success)
        {
            if (FirstError.Code == ErrorCode_Success) { FirstError = UnloadResult; } else { Error_Deconstruct(&UnloadResult); }
        }
    }

    // Destroy every definition (destroying an object does not mutate the map).
    ICollection* DefinitionValues = IMap_AsValueCollection(DefinitionsMap(self));
    CollectionEnumerator* Enumerator = ICollection_CreateEnumerator(DefinitionValues);
    if (Enumerator != NULL)
    {
        while (true)
        {
            bool HasNext = false;
            Error NextCheck = CollectionEnumerator_HasNext(Enumerator, &HasNext);
            if (NextCheck.Code != ErrorCode_Success) { Error_Deconstruct(&NextCheck); break; }
            if (!HasNext) { break; }
            AssetDefinition* Definition = NULL;
            Error NextResult = CollectionEnumerator_NextByValue(Enumerator, &Definition);
            if (NextResult.Code != ErrorCode_Success) { Error_Deconstruct(&NextResult); break; }
            if ((Definition != NULL) && (Definition->VTable != NULL) && (Definition->VTable->Destroy != NULL))
            {
                Definition->VTable->Destroy(Definition);
            }
        }
        CollectionEnumerator_Destroy(Enumerator);
    }

    // Free types.
    size_t TypeCount = IList_GetElementCount(TypesList(self));
    for (size_t i = 0; i < TypeCount; i++)
    {
        TypeRecord* Record = NULL;
        Error GetResult = IList_GetElement(TypesList(self), i, &Record);
        if (GetResult.Code != ErrorCode_Success) { Error_Deconstruct(&GetResult); continue; }
        Memory_Free(Record->Name);
        Memory_Free(Record->DirectoryName);
        Memory_Free(Record);
    }

    // Free search roots.
    size_t RootCount = IList_GetElementCount(RootsList(self));
    for (size_t i = 0; i < RootCount; i++)
    {
        unsigned char* Root = NULL;
        Error GetResult = IList_GetElement(RootsList(self), i, &Root);
        if (GetResult.Code != ErrorCode_Success) { Error_Deconstruct(&GetResult); continue; }
        Memory_Free(Root);
    }

    // Free reference blobs.
    size_t ReferenceCount = IList_GetElementCount(ReferenceList(self));
    for (size_t i = 0; i < ReferenceCount; i++)
    {
        ReferenceBlob* Blob = NULL;
        Error GetResult = IList_GetElement(ReferenceList(self), i, &Blob);
        if (GetResult.Code != ErrorCode_Success) { Error_Deconstruct(&GetResult); continue; }
        Memory_Free(Blob->Name);
        Memory_Free(Blob->Bytes);
        Memory_Free(Blob);
    }

    Error ClearResult = ClearCacheDirectory(self);
    if (ClearResult.Code != ErrorCode_Success)
    {
        if (FirstError.Code == ErrorCode_Success) { FirstError = ClearResult; } else { Error_Deconstruct(&ClearResult); }
    }

    HashMap_Deconstruct(&self->_definitions);
    HashMap_Deconstruct(&self->_loadedAssets);
    HashSet_Deconstruct(&self->_activeUsers);
    ArrayList_Deconstruct(&self->_types);
    ArrayList_Deconstruct(&self->_searchRoots);
    ArrayList_Deconstruct(&self->_referenceBlobs);
    BufferPool_Deconstruct(&self->_bufferPool);
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

    TypeRecord* Record = Memory_Allocate(sizeof(TypeRecord));
    Memory_Zero(Record, sizeof(*Record));
    Record->ID = self->_nextTypeID;
    Record->Name = DuplicateString(info->Name);
    Record->DirectoryName = DuplicateString(info->DirectoryName);
    Record->Constructor = info->Constructor;
    Record->ConstructorUserData = info->ConstructorUserData;

    Error AddResult = IList_AddLast(TypesList(self), &Record);
    if (AddResult.Code != ErrorCode_Success)
    {
        Memory_Free(Record->Name);
        Memory_Free(Record->DirectoryName);
        Memory_Free(Record);
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
        LoadedAssetRecord* AnyLoaded = NULL;
        Error FindResult = FindRecord(self, true, &AnyLoaded);
        if (FindResult.Code != ErrorCode_Success) { return FindResult; }
        if ((AnyLoaded != NULL) && (AnyLoaded->Type == id))
        {
            return Error_Construct3(ErrorCode_InvalidOperation, u8"Cannot remove a type with loaded assets.");
        }

        Error RemoveResult = IList_RemoveAt(List, i);
        if (RemoveResult.Code != ErrorCode_Success) { return RemoveResult; }
        Memory_Free(Record->Name);
        Memory_Free(Record->DirectoryName);
        Memory_Free(Record);
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

Error AssetManager_OpenResource(AssetManager* self, AssetTypeID assetType, const AssetLocation* location, IOStream** outStream)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (location == NULL) { return CreateNullError(u8"location"); }
    if (outStream == NULL) { return CreateNullError(u8"outStream"); }
    *outStream = NULL;

    TypeRecord* Type = FindTypeByID(self, assetType);
    if (Type == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }

    if (location->Type == AssetLocationType_File)
    {
        GenericBuffer FullPath;
        GenericBuffer_AllocateVariable(&FullPath, 0U, 1U);
        bool Found = false;
        Error ResolveResult = ResolveExistingFilePath(self, Type, location->Value, &FullPath, &Found);
        if (ResolveResult.Code != ErrorCode_Success)
        {
            Memory_Free(FullPath._data);
            return ResolveResult;
        }
        if (!Found)
        {
            Memory_Free(FullPath._data);
            return Error_Construct3(ErrorCode_FileNotFound, u8"No file found for \"%s\".", location->Value);
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
    const unsigned char* preferredExtension, AssetResourcePath** outHandle)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (location == NULL) { return CreateNullError(u8"location"); }
    if (outHandle == NULL) { return CreateNullError(u8"outHandle"); }
    *outHandle = NULL;

    TypeRecord* Type = FindTypeByID(self, assetType);
    if (Type == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }

    if (location->Type == AssetLocationType_File)
    {
        GenericBuffer FullPath;
        GenericBuffer_AllocateVariable(&FullPath, 0U, 1U);
        bool Found = false;
        Error ResolveResult = ResolveExistingFilePath(self, Type, location->Value, &FullPath, &Found);
        if (ResolveResult.Code != ErrorCode_Success)
        {
            Memory_Free(FullPath._data);
            return ResolveResult;
        }
        if (!Found)
        {
            Memory_Free(FullPath._data);
            return Error_Construct3(ErrorCode_FileNotFound, u8"No file found for \"%s\".", location->Value);
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

Error AssetManager_ReadDefinitions(AssetManager* self)
{
    if (self == NULL) { return CreateNullError(u8"self"); }

    size_t RootCount = IList_GetElementCount(RootsList(self));
    size_t TypeCount = IList_GetElementCount(TypesList(self));

    GenericBuffer DirectoryPath;
    GenericBuffer FileBytes;
    GenericBuffer_AllocateVariable(&DirectoryPath, 0U, 1U);
    GenericBuffer_AllocateVariable(&FileBytes, 0U, 1U);
    Error Result = Error_CreateSuccess();

    // Process roots in priority order (index 0 = highest); skip-if-exists gives override semantics.
    for (size_t rootIndex = 0; (rootIndex < RootCount) && (Result.Code == ErrorCode_Success); rootIndex++)
    {
        unsigned char* Root = NULL;
        Error GetRootResult = IList_GetElement(RootsList(self), rootIndex, &Root);
        if (GetRootResult.Code != ErrorCode_Success) { Result = GetRootResult; break; }

        for (size_t typeIndex = 0; (typeIndex < TypeCount) && (Result.Code == ErrorCode_Success); typeIndex++)
        {
            TypeRecord* Type = NULL;
            Error GetTypeResult = IList_GetElement(TypesList(self), typeIndex, &Type);
            if (GetTypeResult.Code != ErrorCode_Success) { Result = GetTypeResult; break; }

            GenericBuffer_Clear(&DirectoryPath);
            const unsigned char* Parts[2] = { Root, Type->DirectoryName };
            Error CombineResult = Path_Combine(Parts, 2U, &DirectoryPath);
            if (CombineResult.Code != ErrorCode_Success) { Result = CombineResult; break; }

            DirectoryEntryEnumerator* Enumerator = NULL;
            Error FilesResult = FileSystem_GetFiles(DirectoryPath._data, DirectorySearchOption_All, &Enumerator);
            if ((FilesResult.Code == ErrorCode_DirectoryNotFound) || (FilesResult.Code == ErrorCode_FileNotFound))
            {
                Error_Deconstruct(&FilesResult);
                continue;
            }
            if (FilesResult.Code != ErrorCode_Success) { Result = FilesResult; break; }

            while (Result.Code == ErrorCode_Success)
            {
                bool HasNext = false;
                Error NextCheck = DirectoryEntryEnumerator_HasNext(Enumerator, &HasNext);
                if (NextCheck.Code != ErrorCode_Success) { Result = NextCheck; break; }
                if (!HasNext) { break; }

                FileSystemEntryInfo Info;
                Memory_Zero(&Info, sizeof(Info));
                Error NextResult = DirectoryEntryEnumerator_Next(Enumerator, &Info);
                if (NextResult.Code != ErrorCode_Success) { Result = NextResult; break; }

                GenericBuffer_Clear(&FileBytes);
                Error ReadResult = FileSystem_ReadAllBytes(Info._path, &FileBytes);
                if (ReadResult.Code == ErrorCode_Success)
                {
                    AssetDefinition* Definition = NULL;
                    Error BuildResult = AssetManager_BuildDefinition(self, Type->ID, &FileBytes, Info._path, &Definition);
                    if (BuildResult.Code == ErrorCode_Success)
                    {
                        Result = SetDefinitionInternal(self, Definition, false);
                    }
                    else
                    {
                        Result = BuildResult;
                    }
                }
                else
                {
                    Result = ReadResult;
                }
                FileSystemEntryInfo_Deconstruct(&Info);
            }

            Error DeconstructResult = DirectoryEntryEnumerator_Deconstruct(Enumerator);
            if ((Result.Code == ErrorCode_Success) && (DeconstructResult.Code != ErrorCode_Success)) { Result = DeconstructResult; }
            else { Error_Deconstruct(&DeconstructResult); }
        }
    }

    Memory_Free(DirectoryPath._data);
    Memory_Free(FileBytes._data);
    return Result;
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
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (definition == NULL) { return CreateNullError(u8"definition"); }
    return SetDefinitionInternal(self, definition, true);
}

Error AssetManager_RemoveDefinition(AssetManager* self, AssetTypeID type, const unsigned char* name)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (name == NULL) { return CreateNullError(u8"name"); }

    AssetDefinition* Definition = NULL;
    if (!TryGetDefinition(self, type, name, &Definition))
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"No definition named \"%s\" for the type.", name);
    }

    AssetKey Key = { Definition->Type, Definition->Name };
    bool WasRemoved = false;
    Error RemoveResult = IMap_Remove(DefinitionsMap(self), &Key, &WasRemoved);
    if (RemoveResult.Code != ErrorCode_Success) { return RemoveResult; }
    Definition->VTable->Destroy(Definition);
    return Error_CreateSuccess();
}

Error AssetManager_HasDefinition(AssetManager* self, AssetTypeID type, const unsigned char* name, bool* outExists)
{
    if (self == NULL) { return CreateNullError(u8"self"); }
    if (name == NULL) { return CreateNullError(u8"name"); }
    if (outExists == NULL) { return CreateNullError(u8"outExists"); }
    AssetDefinition* Definition = NULL;
    *outExists = TryGetDefinition(self, type, name, &Definition);
    return Error_CreateSuccess();
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

    if (FindTypeByID(self, assetType) == NULL) { return Error_Construct3(ErrorCode_IllegalArgument, u8"Unknown asset type id."); }
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

    AssetDefinition* Definition = NULL;
    if (!TryGetDefinition(self, assetType, name, &Definition))
    {
        return Error_Construct3(ErrorCode_InvalidOperation, u8"No definition named \"%s\" for the type.", name);
    }

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
