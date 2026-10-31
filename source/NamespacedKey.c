#include <stdio.h>
#include "NamespacedKey.h"
#include "wr/WRCompile.h"
#include "wr/WRComparator.h"
#include "wr/WRHash.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"
#include "wr/WRUserData.h"


// Macros.
/** Bytes of one text block. A key never spans two blocks (it needs at most NAMESPACED_KEY_BUFFER_SIZE bytes), so a
 *  block is filled until the next key does not fit. */
#define KEY_REGISTRY_BLOCK_SIZE ((size_t)16384)
/** The most bytes of a bad key quoted in a message. */
#define KEY_QUOTE_LIMIT ((size_t)64)
/** Room for a quoted key: the quoted bytes, "..." and the terminator. */
#define KEY_QUOTE_CAPACITY (KEY_QUOTE_LIMIT + 4U)
/** The printable ASCII range; other bytes are quoted as '?'. */
#define KEY_PRINTABLE_FIRST 0x20U
#define KEY_PRINTABLE_LAST 0x7EU


// Types.
/* What is wrong with a key, for its message. */
typedef enum KeyProblemEnum
{
    KeyProblem_None,
    KeyProblem_Empty,
    KeyProblem_NoSeparator,
    KeyProblem_EmptyNamespace,
    KeyProblem_EmptyPath,
    KeyProblem_Uppercase,
    KeyProblem_NamespaceCharacter,
    KeyProblem_PathCharacter,
    KeyProblem_TooLong
} KeyProblem;


// Static functions: characters.
static bool IsNamespaceByte(unsigned char byte)
{
    bool IsLetter = (byte >= (unsigned char)'a') && (byte <= (unsigned char)'z');
    bool IsDigit = (byte >= (unsigned char)'0') && (byte <= (unsigned char)'9');
    return IsLetter || IsDigit || (byte == (unsigned char)'_') || (byte == (unsigned char)'.')
        || (byte == (unsigned char)'-');
}

static bool IsPathByte(unsigned char byte)
{
    return IsNamespaceByte(byte) || (byte == (unsigned char)'/');
}

static bool IsUppercaseByte(unsigned char byte)
{
    return (byte >= (unsigned char)'A') && (byte <= (unsigned char)'Z');
}

/* The first problem with a namespace and a path; uppercase is named as such because it is the common mistake. */
static KeyProblem FindPartProblem(const unsigned char* nameSpace, size_t namespaceLength, const unsigned char* path,
    size_t pathLength)
{
    if (namespaceLength == 0U)
    {
        return KeyProblem_EmptyNamespace;
    }
    if (pathLength == 0U)
    {
        return KeyProblem_EmptyPath;
    }
    for (size_t Index = 0; Index < namespaceLength; Index++)
    {
        if (!IsNamespaceByte(nameSpace[Index]))
        {
            return IsUppercaseByte(nameSpace[Index]) ? KeyProblem_Uppercase : KeyProblem_NamespaceCharacter;
        }
    }
    for (size_t Index = 0; Index < pathLength; Index++)
    {
        if (!IsPathByte(path[Index]))
        {
            return IsUppercaseByte(path[Index]) ? KeyProblem_Uppercase : KeyProblem_PathCharacter;
        }
    }
    if ((namespaceLength + 1U + pathLength) > NAMESPACED_KEY_MAX_LENGTH)
    {
        return KeyProblem_TooLong;
    }
    return KeyProblem_None;
}

/* The index of the first colon in @p text, or @p byteCount when there is none. */
static size_t FindSeparator(const unsigned char* text, size_t byteCount)
{
    for (size_t Index = 0; Index < byteCount; Index++)
    {
        if (text[Index] == NAMESPACED_KEY_SEPARATOR)
        {
            return Index;
        }
    }
    return byteCount;
}

/* The first problem with a full key of @p byteCount bytes. */
static KeyProblem FindFullKeyProblem(const unsigned char* key, size_t byteCount)
{
    if (byteCount == 0U)
    {
        return KeyProblem_Empty;
    }
    size_t Separator = FindSeparator(key, byteCount);
    if (Separator == byteCount)
    {
        return KeyProblem_NoSeparator;
    }
    return FindPartProblem(key, Separator, key + Separator + 1U, byteCount - Separator - 1U);
}


// Static functions: messages.
static const char* DescribeProblem(KeyProblem problem)
{
    switch (problem)
    {
        case KeyProblem_None:
            return "it is valid";
        case KeyProblem_Empty:
            return "a key must not be empty";
        case KeyProblem_NoSeparator:
            return "it must be \"namespace:path\"";
        case KeyProblem_EmptyNamespace:
            return "the namespace before the colon is empty";
        case KeyProblem_EmptyPath:
            return "the path after the colon is empty";
        case KeyProblem_Uppercase:
            return "uppercase letters are not allowed (keys are lowercase)";
        case KeyProblem_NamespaceCharacter:
            return "a namespace may only use a-z, 0-9, _, . and -";
        case KeyProblem_PathCharacter:
            return "a path may only use a-z, 0-9, _, ., - and /";
        case KeyProblem_TooLong:
            return "it is longer than 128 bytes";
    }
    return "it is not a key";
}

/* Copies up to KEY_QUOTE_LIMIT bytes of @p text into @p quote, with every byte that is not printable ASCII as '?',
 * so a hostile key cannot put control characters or broken UTF-8 into a message. */
static void QuoteKey(const unsigned char* text, size_t byteCount, char quote[KEY_QUOTE_CAPACITY])
{
    size_t Count = (byteCount < KEY_QUOTE_LIMIT) ? byteCount : KEY_QUOTE_LIMIT;
    for (size_t Index = 0; Index < Count; Index++)
    {
        unsigned char Byte = text[Index];
        bool IsPrintable = (Byte >= KEY_PRINTABLE_FIRST) && (Byte <= KEY_PRINTABLE_LAST);
        quote[Index] = IsPrintable ? (char)Byte : '?';
    }
    size_t End = Count;
    if (Count < byteCount)
    {
        quote[End++] = '.';
        quote[End++] = '.';
        quote[End++] = '.';
    }
    quote[End] = '\0';
}

static Error MakeProblemError(const unsigned char* text, size_t byteCount, KeyProblem problem)
{
    if (problem == KeyProblem_Empty)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "A namespaced key must not be empty.");
    }
    char Quote[KEY_QUOTE_CAPACITY];
    QuoteKey(text, byteCount, Quote);
    return Error_Construct4(ErrorCode_ArgumentOutOfRange, "Namespaced key \"%s\": %s.", Quote, DescribeProblem(problem));
}


// Static functions: the registry.
static HashCode HashTextHash(IMap* map, const void* key, const UserData* userData)
{
    UNUSED(map);
    UNUSED(userData);
    // The keys are already FNV-1a hashes of the texts.
    HashCode Hash = 0U;
    Memory_Copy(key, &Hash, sizeof(Hash));
    return Hash;
}

static KeyRegistryLocation GetLocationAt(const KeyRegistry* self, uint32_t id)
{
    const KeyRegistryLocation* Locations = (const KeyRegistryLocation*)self->_locations._data;
    return Locations[id - 1U];
}

static const unsigned char* GetTextAt(const KeyRegistry* self, uint32_t id)
{
    KeyRegistryLocation Location = GetLocationAt(self, id);
    const unsigned char* const* Blocks = (const unsigned char* const*)self->_blocks._data;
    return Blocks[Location.Block] + Location.Offset;
}

static uint32_t GetNextWithSameHash(const KeyRegistry* self, uint32_t id)
{
    const uint32_t* Next = (const uint32_t*)self->_nextWithSameHash._data;
    return Next[id - 1U];
}

/* The id of the most recent key with @p hash, or 0. ContainsKey first: a miss on GetElement builds an error message. */
static uint32_t FindFirstWithHash(KeyRegistry* self, HashCode hash)
{
    IMap* Map = HashMap_AsMap(&self->_firstIdByHash);
    bool IsPresent = false;
    Error ContainsResult = IMap_ContainsKey(Map, &hash, &IsPresent);
    if ((ContainsResult.Code != ErrorCode_Success) || !IsPresent)
    {
        Error_Deconstruct(&ContainsResult);
        return NAMESPACED_KEY_ID_NONE;
    }
    uint32_t Id = NAMESPACED_KEY_ID_NONE;
    Error GetResult = IMap_GetElement(Map, &hash, &Id);
    if (GetResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&GetResult);
        return NAMESPACED_KEY_ID_NONE;
    }
    return Id;
}

/* Whether a stored (terminated) key is exactly the @p byteCount bytes of @p text, which hold no NUL: the comparison
 * stops at the stored key's terminator, so a shorter key is never read past its end. */
static bool IsText(const unsigned char* stored, const unsigned char* text, size_t byteCount)
{
    for (size_t Index = 0U; Index < byteCount; Index++)
    {
        if (stored[Index] != text[Index])
        {
            return false;
        }
    }
    return stored[byteCount] == (unsigned char)0;
}

/* The id of the key whose text is the @p byteCount bytes of @p text (which hash to @p hash), or 0. */
static uint32_t FindText(KeyRegistry* self, const unsigned char* text, size_t byteCount, HashCode hash)
{
    for (uint32_t Id = FindFirstWithHash(self, hash); Id != NAMESPACED_KEY_ID_NONE; Id = GetNextWithSameHash(self, Id))
    {
        if (IsText(GetTextAt(self, Id), text, byteCount))
        {
            return Id;
        }
    }
    return NAMESPACED_KEY_ID_NONE;
}

/* Copies @p byteCount bytes of text and a terminator into the blocks (which never move); false when no block could be
 * added. */
static bool StoreText(KeyRegistry* self, const unsigned char* text, size_t byteCount, KeyRegistryLocation* outLocation)
{
    if ((self->_blocks._count == 0U) || ((KEY_REGISTRY_BLOCK_SIZE - self->_lastBlockUsed) < (byteCount + 1U)))
    {
        unsigned char* Block = Memory_Allocate(KEY_REGISTRY_BLOCK_SIZE);
        if (!GenericBuffer_AddLast(&self->_blocks, &Block))
        {
            Memory_Free(Block);
            return false;
        }
        self->_lastBlockUsed = 0U;
    }
    unsigned char* Block = NULL;
    GenericBuffer_GetLast(&self->_blocks, &Block);
    unsigned char* Text = Block + self->_lastBlockUsed;
    Memory_Copy(text, Text, byteCount);
    Text[byteCount] = (unsigned char)0;
    *outLocation = (KeyRegistryLocation){ .Block = (uint32_t)(self->_blocks._count - 1U),
        .Offset = (uint32_t)self->_lastBlockUsed };
    self->_lastBlockUsed += byteCount + 1U;
    return true;
}

/* Frees the blocks from index @p first on. */
static void FreeBlocksFrom(KeyRegistry* self, size_t first)
{
    for (size_t Index = first; Index < self->_blocks._count; Index++)
    {
        unsigned char* Block = NULL;
        GenericBuffer_GetAt(&self->_blocks, Index, &Block);
        Memory_Free(Block);
    }
    if (first < self->_blocks._count)
    {
        GenericBuffer_SetCount(&self->_blocks, first);
    }
}

/* Adds a new key (known to be valid and absent) with its hash; returns its id through @p outId. */
static Error AddKey(KeyRegistry* self, const unsigned char* key, size_t byteCount, HashCode hash, uint32_t* outId)
{
    if (KeyRegistry_GetCount(self) >= KEY_REGISTRY_MAX_KEY_COUNT)
    {
        return Error_Construct2(ErrorCode_InvalidOperation,
            "KeyRegistry_Intern: the registry is full (1048576 keys); no more keys can be interned.");
    }

    KeyRegistryLocation Location = { .Block = 0U, .Offset = 0U };
    uint32_t Next = FindFirstWithHash(self, hash);
    if (!StoreText(self, key, byteCount, &Location) || !GenericBuffer_AddLast(&self->_locations, &Location))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "KeyRegistry_Intern: the key could not be stored.");
    }
    if (!GenericBuffer_AddLast(&self->_nextWithSameHash, &Next))
    {
        GenericBuffer_RemoveLast(&self->_locations);
        return Error_Construct2(ErrorCode_BufferTooSmall, "KeyRegistry_Intern: the key could not be stored.");
    }

    // The new key becomes the first of its hash's chain (the map overwrites the old first id).
    uint32_t Id = (uint32_t)KeyRegistry_GetCount(self);
    bool WasAdded = false;
    Error MapResult = IMap_Add(HashMap_AsMap(&self->_firstIdByHash), &hash, &Id, &WasAdded);
    if (MapResult.Code != ErrorCode_Success)
    {
        GenericBuffer_RemoveLast(&self->_locations);
        GenericBuffer_RemoveLast(&self->_nextWithSameHash);
        return MapResult;
    }
    *outId = Id;
    return Error_CreateSuccess();
}

/* Orders ids by their text (the registry is stored inline in the user data). */
static ComparisonResult CompareIdsByText(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    UNUSED(buffer);
    const KeyRegistry* Registry = NULL;
    Memory_Copy(userData->Bytes, &Registry, sizeof(Registry));
    uint32_t First = 0U;
    uint32_t Second = 0U;
    Memory_Copy(a._element, &First, sizeof(First));
    Memory_Copy(b._element, &Second, sizeof(Second));
    const unsigned char* FirstText = GetTextAt(Registry, First);
    const unsigned char* SecondText = GetTextAt(Registry, Second);
    size_t Index = 0U;
    while ((FirstText[Index] == SecondText[Index]) && (FirstText[Index] != (unsigned char)0))
    {
        Index++;
    }
    return COMPARE_NUMBER(FirstText[Index], SecondText[Index]);
}


// Public functions: keys.
bool NamespacedKey_IsValid(const unsigned char* key)
{
    if (key == NULL)
    {
        return false;
    }
    size_t Length = 0U;
    while ((key[Length] != (unsigned char)0) && (Length <= NAMESPACED_KEY_MAX_LENGTH))
    {
        Length++;
    }
    return (Length <= NAMESPACED_KEY_MAX_LENGTH) && (FindFullKeyProblem(key, Length) == KeyProblem_None);
}

Error NamespacedKey_Validate(const unsigned char* key)
{
    if (key == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "NamespacedKey_Validate: key must not be NULL.");
    }
    size_t Length = StringUTF8_GetByteLength(key);
    KeyProblem Problem = (Length > NAMESPACED_KEY_MAX_LENGTH) ? KeyProblem_TooLong : FindFullKeyProblem(key, Length);
    return (Problem == KeyProblem_None) ? Error_CreateSuccess() : MakeProblemError(key, Length, Problem);
}

bool NamespacedKey_IsNamespaceValid(const unsigned char* text, size_t byteCount)
{
    // Room for the colon and a path of at least one byte.
    if ((text == NULL) || (byteCount == 0U) || (byteCount > (NAMESPACED_KEY_MAX_LENGTH - 2U)))
    {
        return false;
    }
    for (size_t Index = 0; Index < byteCount; Index++)
    {
        if (!IsNamespaceByte(text[Index]))
        {
            return false;
        }
    }
    return true;
}

Error NamespacedKey_Parse(const unsigned char* text, size_t byteCount, const unsigned char* defaultNamespace,
    GenericBuffer* outKey)
{
    const unsigned char* Default = (defaultNamespace != NULL) ? defaultNamespace : NAMESPACED_KEY_DEFAULT_NAMESPACE;
    size_t DefaultLength = StringUTF8_GetByteLength(Default);
    if ((outKey == NULL) || (outKey->_elementSize != sizeof(unsigned char))
        || !NamespacedKey_IsNamespaceValid(Default, DefaultLength))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "NamespacedKey_Parse: outKey must be a byte buffer and defaultNamespace a valid namespace.");
    }
    if ((text == NULL) || (byteCount == 0U))
    {
        return MakeProblemError(text, 0U, KeyProblem_Empty);
    }

    size_t Separator = FindSeparator(text, byteCount);
    bool HasNamespace = Separator < byteCount;
    const unsigned char* Namespace = HasNamespace ? text : Default;
    size_t NamespaceLength = HasNamespace ? Separator : DefaultLength;
    const unsigned char* Path = HasNamespace ? (text + Separator + 1U) : text;
    size_t PathLength = HasNamespace ? (byteCount - Separator - 1U) : byteCount;
    KeyProblem Problem = FindPartProblem(Namespace, NamespaceLength, Path, PathLength);
    if (Problem != KeyProblem_None)
    {
        return MakeProblemError(text, byteCount, Problem);
    }

    // Reserve first, so nothing is appended when the buffer cannot hold the whole key.
    bool HasTerminator = (outKey->_count > 0U) && (((unsigned char*)outKey->_data)[outKey->_count - 1U] == 0U);
    size_t KeyLength = NamespaceLength + 1U + PathLength;
    if (!GenericBuffer_TryPrepareForManualMutation(outKey, KeyLength + (HasTerminator ? 0U : 1U)))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "NamespacedKey_Parse: outKey cannot hold the key.");
    }
    if (HasTerminator)
    {
        GenericBuffer_SetCount(outKey, outKey->_count - 1U);
    }
    void* Tail = NULL;
    if (!GenericBuffer_GetWritableTail(outKey, KeyLength + 1U, &Tail))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "NamespacedKey_Parse: outKey cannot hold the key.");
    }
    unsigned char* Bytes = Tail;
    Memory_Copy(Namespace, Bytes, NamespaceLength);
    Bytes[NamespaceLength] = NAMESPACED_KEY_SEPARATOR;
    Memory_Copy(Path, Bytes + NamespaceLength + 1U, PathLength);
    Bytes[KeyLength] = (unsigned char)0;
    GenericBuffer_CommitCount(outKey, KeyLength + 1U);
    return Error_CreateSuccess();
}

const char* NamespacedKey_DescribeProblem(const unsigned char* text, size_t byteCount,
    const unsigned char* defaultNamespace)
{
    const unsigned char* Default = (defaultNamespace != NULL) ? defaultNamespace : NAMESPACED_KEY_DEFAULT_NAMESPACE;
    if ((text == NULL) || (byteCount == 0U))
    {
        return DescribeProblem(KeyProblem_Empty);
    }
    size_t Separator = FindSeparator(text, byteCount);
    bool HasNamespace = Separator < byteCount;
    KeyProblem Problem = HasNamespace
        ? FindPartProblem(text, Separator, text + Separator + 1U, byteCount - Separator - 1U)
        : FindPartProblem(Default, StringUTF8_GetByteLength(Default), text, byteCount);
    return (Problem == KeyProblem_None) ? NULL : DescribeProblem(Problem);
}

bool NamespacedKey_IsValidLocalName(const unsigned char* name)
{
    if ((name == NULL) || (name[0] < (unsigned char)'a') || (name[0] > (unsigned char)'z'))
    {
        return false;
    }
    for (size_t Index = 1U; name[Index] != (unsigned char)0; Index++)
    {
        if (!IsNamespaceByte(name[Index]))
        {
            return false;
        }
    }
    return true;
}

size_t NamespacedKey_GetNamespaceLength(const unsigned char* key)
{
    if (key == NULL)
    {
        return 0U;
    }
    for (size_t Index = 0; key[Index] != (unsigned char)0; Index++)
    {
        if (key[Index] == NAMESPACED_KEY_SEPARATOR)
        {
            return Index;
        }
    }
    return 0U;
}

const unsigned char* NamespacedKey_GetPath(const unsigned char* key)
{
    if (key == NULL)
    {
        return NULL;
    }
    for (size_t Index = 0; key[Index] != (unsigned char)0; Index++)
    {
        if (key[Index] == NAMESPACED_KEY_SEPARATOR)
        {
            return key + Index + 1U;
        }
    }
    return NULL;
}


// Public functions: the registry.
Error KeyRegistry_Construct(KeyRegistry* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "KeyRegistry_Construct: self must not be NULL.");
    }
    HashMapConstructOptions Options = HashMapConstructOptions_CreateDefault(sizeof(HashCode), sizeof(uint32_t),
        HashTextHash);
    Error MapResult = HashMap_Construct1(&self->_firstIdByHash, Options);
    if (MapResult.Code != ErrorCode_Success)
    {
        return MapResult;
    }
    GenericBuffer_AllocateVariable(&self->_locations, 0U, sizeof(KeyRegistryLocation));
    GenericBuffer_AllocateVariable(&self->_nextWithSameHash, 0U, sizeof(uint32_t));
    GenericBuffer_AllocateVariable(&self->_blocks, 0U, sizeof(unsigned char*));
    self->_lastBlockUsed = 0U;
    return Error_CreateSuccess();
}

void KeyRegistry_Deconstruct(KeyRegistry* self)
{
    if (self == NULL)
    {
        return;
    }
    FreeBlocksFrom(self, 0U);
    Memory_Free(self->_blocks._data);
    Memory_Free(self->_locations._data);
    Memory_Free(self->_nextWithSameHash._data);
    Error MapResult = HashMap_Deconstruct(&self->_firstIdByHash);
    Error_Deconstruct(&MapResult);
    Memory_Zero(self, sizeof(*self));
}

Error KeyRegistry_Intern(KeyRegistry* self, const unsigned char* key, uint32_t* outId)
{
    if ((self == NULL) || (key == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "KeyRegistry_Intern: self, key and outId must not be NULL.");
    }
    Error CheckResult = NamespacedKey_Validate(key);
    if (CheckResult.Code != ErrorCode_Success)
    {
        return CheckResult;
    }

    size_t Length = StringUTF8_GetByteLength(key);
    HashCode Hash = Hash_HashBytes(key, Length);
    uint32_t Found = FindText(self, key, Length, Hash);
    if (Found != NAMESPACED_KEY_ID_NONE)
    {
        *outId = Found;
        return Error_CreateSuccess();
    }
    return AddKey(self, key, Length, Hash, outId);
}

Error KeyRegistry_InternText(KeyRegistry* self, const unsigned char* text, size_t byteCount,
    const unsigned char* defaultNamespace, uint32_t* outId)
{
    if ((self == NULL) || (outId == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "KeyRegistry_InternText: self and outId must not be NULL.");
    }
    unsigned char Storage[NAMESPACED_KEY_BUFFER_SIZE];
    GenericBuffer Key;
    GenericBuffer_CreateConstant(&Key, Storage, sizeof(Storage), sizeof(unsigned char), 0U);
    Error ParseResult = NamespacedKey_Parse(text, byteCount, defaultNamespace, &Key);
    if (ParseResult.Code != ErrorCode_Success)
    {
        return ParseResult;
    }
    return KeyRegistry_Intern(self, Storage, outId);
}

bool KeyRegistry_TryFind(KeyRegistry* self, const unsigned char* key, uint32_t* outId)
{
    if ((self == NULL) || (key == NULL) || (outId == NULL))
    {
        return false;
    }
    size_t Length = 0U;
    while ((key[Length] != (unsigned char)0) && (Length <= NAMESPACED_KEY_MAX_LENGTH))
    {
        Length++;
    }
    if ((Length == 0U) || (Length > NAMESPACED_KEY_MAX_LENGTH))
    {
        return false;
    }
    uint32_t Found = FindText(self, key, Length, Hash_HashBytes(key, Length));
    if (Found == NAMESPACED_KEY_ID_NONE)
    {
        return false;
    }
    *outId = Found;
    return true;
}

void KeyRegistry_TruncateTo(KeyRegistry* self, size_t count)
{
    if ((self == NULL) || (count >= KeyRegistry_GetCount(self)))
    {
        return;
    }

    // The newest key heads its hash's chain (interning puts each new key first), so forgetting the keys from the newest
    // on hands each chain back to the key that headed it before.
    IMap* Map = HashMap_AsMap(&self->_firstIdByHash);
    for (size_t Id = KeyRegistry_GetCount(self); Id > count; Id--)
    {
        const unsigned char* Text = GetTextAt(self, (uint32_t)Id);
        HashCode Hash = Hash_HashBytes(Text, StringUTF8_GetByteLength(Text));
        uint32_t Next = GetNextWithSameHash(self, (uint32_t)Id);
        bool IsDone = false;
        Error MapResult = (Next == NAMESPACED_KEY_ID_NONE) ? IMap_Remove(Map, &Hash, &IsDone)
            : IMap_Add(Map, &Hash, &Next, &IsDone);
        Error_Deconstruct(&MapResult); // The key is present and the arguments valid; allocation failures abort.
    }

    // The texts are stored in id order: the storage now ends right after the text of the newest key kept.
    if (count == 0U)
    {
        FreeBlocksFrom(self, 0U);
        self->_lastBlockUsed = 0U;
    }
    else
    {
        KeyRegistryLocation Last = GetLocationAt(self, (uint32_t)count);
        const unsigned char* Text = GetTextAt(self, (uint32_t)count);
        FreeBlocksFrom(self, (size_t)Last.Block + 1U);
        self->_lastBlockUsed = (size_t)Last.Offset + StringUTF8_GetByteLength(Text) + 1U;
    }
    GenericBuffer_SetCount(&self->_locations, count);
    GenericBuffer_SetCount(&self->_nextWithSameHash, count);
}

const unsigned char* KeyRegistry_GetText(const KeyRegistry* self, uint32_t id)
{
    if ((self == NULL) || (id == NAMESPACED_KEY_ID_NONE) || ((size_t)id > KeyRegistry_GetCount(self)))
    {
        return NULL;
    }
    return GetTextAt(self, id);
}

Error KeyRegistry_FindByPrefix(const KeyRegistry* self, const unsigned char* prefix, GenericBuffer* outIds)
{
    if ((self == NULL) || (prefix == NULL) || (outIds == NULL) || (outIds->_elementSize != sizeof(uint32_t)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "KeyRegistry_FindByPrefix: self, prefix and outIds (a buffer of uint32_t) must not be NULL.");
    }

    size_t PrefixLength = StringUTF8_GetByteLength(prefix);
    size_t Start = outIds->_count;
    for (size_t Index = 0; Index < KeyRegistry_GetCount(self); Index++)
    {
        uint32_t Id = (uint32_t)(Index + 1U);
        const unsigned char* Text = GetTextAt(self, Id);
        size_t Matched = 0U;
        while ((Matched < PrefixLength) && (Text[Matched] == prefix[Matched]))
        {
            Matched++;
        }
        if ((Matched == PrefixLength) && !GenericBuffer_AddLast(outIds, &Id))
        {
            GenericBuffer_SetCount(outIds, Start);
            return Error_Construct2(ErrorCode_BufferTooSmall, "KeyRegistry_FindByPrefix: outIds could not grow.");
        }
    }

    // Sort only the appended ids, through a view over them.
    size_t Added = outIds->_count - Start;
    if (Added < 2U)
    {
        return Error_CreateSuccess();
    }
    GenericBuffer View;
    GenericBuffer_CreateConstant(&View, (uint32_t*)outIds->_data + Start, Added, sizeof(uint32_t), Added);
    UserData Context = UserData_CreateEmpty();
    Memory_Copy(&self, Context.Bytes, sizeof(self));
    uint32_t Scratch[2];
    GenericBuffer_SortAscending(&View, CompareIdsByText, &Context, Scratch);
    return Error_CreateSuccess();
}
