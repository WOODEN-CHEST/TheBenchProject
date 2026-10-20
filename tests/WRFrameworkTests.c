#include "TestSuites.h"
#include "TestText.h"
#include "wr/WRArrayList.h"
#include "wr/WRComparator.h"
#include "wr/WRFileSystem.h"
#include "wr/WRGHDF.h"
#include "wr/WRIO.h"
#include "wr/WRJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRMemoryStream.h"
#include "wr/WRObjectPool.h"
#include "wr/WRPath.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/**
 * WRFramework behaviours the game relies on since T09 (world files), changed in ../WRFramework at the owner's request:
 * JSON compounds keep insertion order and pretty output keeps arrays of scalars on one line; GHDF strings cannot make
 * the reader reserve what the data does not hold, a pool frees containers still borrowed without touching freed pools,
 * truncated documents do not leak; both readers can refuse a document whose tree would take too much memory;
 * FileSystem_ReplaceEntry replaces a file atomically; the sorts stay O(n log n) on sorted input; object pools borrow in
 * O(1) and find a returned object by address. The library has no tests of its own, so they live here.
 */


// Macros.
#define FRAMEWORK_TEST_TEXT_CAPACITY ((size_t)256)
/** A string longer than four of the GHDF reader's 64 KiB chunks. */
#define FRAMEWORK_TEST_LONG_STRING_LENGTH ((size_t)200003)
/** Elements of the large sort cases: past the ~87 000 where the old quicksort overflowed the stack on sorted input (it
 *  recursed once per element, and took minutes before that). */
#define FRAMEWORK_TEST_SORT_COUNT ((size_t)100000)
/** The small sort cases cover every length up to this (the introsort switches to insertion sort at 16). */
#define FRAMEWORK_TEST_SMALL_SORT_LENGTH ((size_t)64)
/** Objects the pool tests borrow: enough for many sections. */
#define FRAMEWORK_TEST_POOL_COUNT ((size_t)100000)
/** Containers in the read limit documents, and limits the documents pass or exceed. */
#define FRAMEWORK_TEST_LIMIT_CONTAINER_COUNT ((size_t)1000)
#define FRAMEWORK_TEST_SMALL_TREE_LIMIT ((size_t)100000)
#define FRAMEWORK_TEST_LARGE_TREE_LIMIT ((size_t)16777216)


// Types.
/* The inputs the sorts are tried on: the old quicksort took its last element as the pivot and recursed into both
 * sides, so sorted, reversed and equal input made it quadratic and recurse once per element. */
typedef enum SortPatternEnum
{
    SortPattern_Ascending,
    SortPattern_Descending,
    SortPattern_AllEqual,
    SortPattern_OrganPipe,
    SortPattern_FewDistinct,
    SortPattern_Random,
    SortPattern_Count
} SortPattern;

/* What a pool's lifecycle callbacks saw. */
typedef struct PoolCountsStruct
{
    size_t Constructed;
    size_t Deconstructed;
} PoolCounts;

/* A readable stream that cannot seek (like a socket), over a byte array: the GHDF reader cannot ask it how much is
 * left, so it must read strings in chunks. */
typedef struct ForwardStreamStruct
{
    IOStream Base;
    const unsigned char* Bytes;
    size_t Count;
    size_t Position;
} ForwardStream;


// Fields.
static const unsigned char GHDFSignature[16] = { 102, 37, 143, 181, 3, 205, 123, 185, 148, 157, 98, 177, 178, 151, 43, 170 };


// Static functions: the forward-only stream.
static Error ForwardStream_Read(void* self, GenericBuffer* destination, size_t readSize)
{
    ForwardStream* Stream = self;
    size_t Available = Stream->Count - Stream->Position;
    size_t Count = (readSize < Available) ? readSize : Available;
    if ((Count > 0U) && !GenericBuffer_AppendRangeBytes(destination, (unsigned char*)Stream->Bytes + Stream->Position,
        Count))
    {
        return Error_Construct2(ErrorCode_BufferTooSmall, "could not append");
    }
    Stream->Position += Count;
    return Error_CreateSuccess();
}

static Error ForwardStream_ReadByte(void* self, unsigned char* byte)
{
    ForwardStream* Stream = self;
    if (Stream->Position >= Stream->Count)
    {
        return Error_Construct2(ErrorCode_IO, "the stream ended");
    }
    *byte = Stream->Bytes[Stream->Position];
    Stream->Position++;
    return Error_CreateSuccess();
}

static bool ForwardStream_IsEndOfStream(void* self)
{
    ForwardStream* Stream = self;
    return Stream->Position >= Stream->Count;
}

static Error ForwardStream_Close(void* self)
{
    (void)self;
    return Error_CreateSuccess();
}

static void ForwardStream_Construct(ForwardStream* self, const unsigned char* bytes, size_t count)
{
    memset(self, 0, sizeof(*self));
    self->Bytes = bytes;
    self->Count = count;
    self->Base._type = IOStreamType_Unknown;
    self->Base._flags = IOStreamFlags_CanRead;
    self->Base._vtable.Self = self;
    self->Base._vtable._read = ForwardStream_Read;
    self->Base._vtable._readByte = ForwardStream_ReadByte;
    self->Base._vtable._isEOF = ForwardStream_IsEndOfStream;
    self->Base._vtable._close = ForwardStream_Close;
    self->Base._vtable._deconstruct = ForwardStream_Close;
}


// Static functions: sorting.
static uint32_t NextRandom(uint32_t* state)
{
    // xorshift32: the same sequence on every platform.
    *state ^= *state << 13U;
    *state ^= *state >> 17U;
    *state ^= *state << 5U;
    return *state;
}

static uint32_t GetPatternValue(SortPattern pattern, size_t index, size_t count, uint32_t* state)
{
    switch (pattern)
    {
        case SortPattern_Ascending:
            return (uint32_t)index;
        case SortPattern_Descending:
            return (uint32_t)(count - index);
        case SortPattern_AllEqual:
            return 7U;
        case SortPattern_OrganPipe:
            return (uint32_t)((index < (count / 2U)) ? index : (count - index));
        case SortPattern_FewDistinct:
            return NextRandom(state) % 4U;
        case SortPattern_Random:
        case SortPattern_Count:
            return NextRandom(state);
    }
    return 0U;
}

static int CompareUInt32ForQsort(const void* a, const void* b)
{
    uint32_t Left = *(const uint32_t*)a;
    uint32_t Right = *(const uint32_t*)b;
    return (Left > Right) - (Left < Right);
}

static ComparisonResult CompareBufferUInt32(GenericBuffer* buffer, GenericBufferElementData a, GenericBufferElementData b,
    const UserData* userData)
{
    (void)buffer;
    (void)userData;
    uint32_t Left = *(const uint32_t*)a._element;
    uint32_t Right = *(const uint32_t*)b._element;
    return COMPARE_NUMBER(Left, Right);
}

static ComparisonResult CompareListUInt32(IList* list, IListElementData a, IListElementData b, const UserData* userData)
{
    (void)list;
    (void)userData;
    uint32_t Left = *(const uint32_t*)a._element;
    uint32_t Right = *(const uint32_t*)b._element;
    return COMPARE_NUMBER(Left, Right);
}

static ComparisonResult CompareBufferAddresses(GenericBuffer* buffer, GenericBufferElementData a,
    GenericBufferElementData b, const UserData* userData)
{
    (void)buffer;
    (void)userData;
    uintptr_t Left = (uintptr_t)*(void* const*)a._element;
    uintptr_t Right = (uintptr_t)*(void* const*)b._element;
    return COMPARE_NUMBER(Left, Right);
}

/* Sorts @p count values of @p pattern ascending, then descending, with both sorts, checking each against qsort. */
static void CheckSorts(TestRun* run, SortPattern pattern, size_t count, uint32_t* state)
{
    uint32_t* Expected = Memory_Allocate((count + 1U) * sizeof(uint32_t));
    GenericBuffer Buffer;
    GenericBuffer_AllocateVariable(&Buffer, count + 1U, sizeof(uint32_t));
    ArrayList List;
    ArrayList_Construct1(&List, sizeof(uint32_t));
    bool IsFilled = true;
    for (size_t Index = 0U; Index < count; Index++)
    {
        uint32_t Value = GetPatternValue(pattern, Index, count, state);
        Expected[Index] = Value;
        Error AddResult = IList_AddLast(&List._list, &Value);
        IsFilled = IsFilled && GenericBuffer_AddLast(&Buffer, &Value) && (AddResult.Code == ErrorCode_Success);
        Error_Deconstruct(&AddResult);
    }
    TEST_CHECK(run, IsFilled);
    qsort(Expected, count, sizeof(uint32_t), CompareUInt32ForQsort);
    uint32_t Scratch[2];
    for (int Direction = 0; Direction < 2; Direction++)
    {
        bool IsAscending = Direction == 0;
        TEST_CHECK(run, IsAscending ? GenericBuffer_SortAscending(&Buffer, CompareBufferUInt32, NULL, Scratch)
            : GenericBuffer_SortDescending(&Buffer, CompareBufferUInt32, NULL, Scratch));
        TEST_CHECK_SUCCESS(run, IsAscending ? IList_SortAscending(&List._list, CompareListUInt32, NULL, Scratch)
            : IList_SortDescending(&List._list, CompareListUInt32, NULL, Scratch));
        bool IsBufferRight = Buffer._count == count;
        bool IsListRight = IList_GetElementCount(&List._list) == count;
        for (size_t Index = 0U; (Index < count) && (IsBufferRight || IsListRight); Index++)
        {
            uint32_t Want = Expected[IsAscending ? Index : (count - 1U - Index)];
            uint32_t FromList = 0U;
            IsBufferRight = IsBufferRight && (((const uint32_t*)Buffer._data)[Index] == Want);
            IsListRight = IsListRight && (IList_GetElement(&List._list, Index, &FromList).Code == ErrorCode_Success)
                && (FromList == Want);
        }
        if (!TEST_CHECK(run, IsBufferRight && IsListRight))
        {
            printf("      pattern %d, %zu elements, %s: buffer %s, list %s\n", (int)pattern, count,
                IsAscending ? "ascending" : "descending", IsBufferRight ? "right" : "wrong", IsListRight ? "right" : "wrong");
        }
    }
    ArrayList_Deconstruct(&List);
    Memory_Free(Buffer._data);
    Memory_Free(Expected);
}


// Static functions: pools.
static Error CountConstructedObject(void* object, const UserData* userData)
{
    (void)object;
    ((PoolCounts*)UserData_GetPointer(userData))->Constructed++;
    return Error_CreateSuccess();
}

static Error CountDeconstructedObject(void* object, const UserData* userData)
{
    (void)object;
    ((PoolCounts*)UserData_GetPointer(userData))->Deconstructed++;
    return Error_CreateSuccess();
}

/* Whether the @p count objects of @p elementSize bytes at @p objects (sorted by address in place) are all distinct and
 * none overlaps the next. */
static bool AreObjectsSeparate(GenericBuffer* objects, size_t elementSize)
{
    void* Scratch[2];
    if (!GenericBuffer_SortAscending(objects, CompareBufferAddresses, NULL, Scratch))
    {
        return false;
    }
    void* const* Sorted = (void* const*)objects->_data;
    for (size_t Index = 1U; Index < objects->_count; Index++)
    {
        if (((uintptr_t)Sorted[Index] - (uintptr_t)Sorted[Index - 1U]) < elementSize)
        {
            return false;
        }
    }
    return true;
}

static bool IsZero(const unsigned char* bytes, size_t count)
{
    for (size_t Index = 0U; Index < count; Index++)
    {
        if (bytes[Index] != 0U)
        {
            return false;
        }
    }
    return true;
}


// Static functions: helpers.
static bool SerializeTo(TestRun* run, JSONObjectValue* value, JSONSerializeFlags flags, GenericBuffer* outText)
{
    return TEST_CHECK_SUCCESS(run, JSON_Serialize(value, outText, (JSONSerializeFlags)(flags | JSONSerializeFlags_NullTerminated)));
}

static bool TextEquals(const GenericBuffer* text, const char* expected)
{
    return (text->_count == (strlen(expected) + 1U)) && (memcmp(text->_data, expected, strlen(expected)) == 0);
}

static void AppendVarint(GenericBuffer* bytes, uint64_t value)
{
    do
    {
        unsigned char Byte = (unsigned char)(value & 0x7FU);
        value >>= 7U;
        if (value != 0U)
        {
            Byte |= 0x80U;
        }
        GenericBuffer_AppendByte(bytes, Byte);
    }
    while (value != 0U);
}

/* A GHDF document of one string entry declaring @p declaredLength bytes, followed by @p presentLength of them. */
static void BuildStringDocument(GenericBuffer* bytes, uint64_t declaredLength, size_t presentLength)
{
    GenericBuffer_AppendRangeBytes(bytes, (unsigned char*)GHDFSignature, sizeof(GHDFSignature));
    AppendVarint(bytes, 1U);
    AppendVarint(bytes, 1U);
    AppendVarint(bytes, 1U);
    GenericBuffer_AppendByte(bytes, 12U);
    AppendVarint(bytes, declaredLength);
    for (size_t Index = 0U; Index < presentLength; Index++)
    {
        GenericBuffer_AppendByte(bytes, (unsigned char)('a' + (Index % 26U)));
    }
}

/* Reads a GHDF document from @p bytes through a seekable memory stream (or a forward-only one), returning the root to
 * the pool on success. */
static Error ReadDocument(GHDFObjectPool* pool, const GenericBuffer* bytes, bool isSeekable, GHDFCompound** outRoot)
{
    *outRoot = NULL;
    if (!isSeekable)
    {
        ForwardStream Stream;
        ForwardStream_Construct(&Stream, bytes->_data, bytes->_count);
        return GHDF_Read(&Stream.Base, pool, outRoot);
    }
    GenericBuffer Source;
    GenericBuffer_CreateReadOnly(&Source, bytes->_data, bytes->_count, sizeof(unsigned char), bytes->_count);
    MemoryStream Stream;
    Error Result = MemoryStream_Construct2(&Stream, &Source, IOStreamFlags_CanRead);
    if (Result.Code == ErrorCode_Success)
    {
        Result = GHDF_Read(MemoryStream_AsIOStream(&Stream), pool, outRoot);
        MemoryStream_Deconstruct(&Stream);
    }
    return Result;
}


// Test cases: JSON.
static void JSONCompoundsKeepInsertionOrder(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    JSONObjectValue Root;
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool))
        && TestText_ParseJSON(run, Pool, "{\"b\": 1, \"a\": 2, \"c\": 3, \"a\": 4}", &Root))
    {
        // Text order, a repeated key keeping its first place and its last value.
        TEST_CHECK(run, SerializeTo(run, &Root, JSONSerializeFlags_None, &Text) && TextEquals(&Text, "{\"b\":1,\"a\":4,\"c\":3}"));

        // Setting a present key keeps its place; a removed key comes back last.
        JSONObjectValue Value = { .Type = JSONValueType_Integer, .Value.Integer = 5 };
        TEST_CHECK_SUCCESS(run, JSONCompound_Set(Root.Value.Compound, u8"b", &Value));
        TEST_CHECK_SUCCESS(run, JSONCompound_Remove(Root.Value.Compound, u8"a"));
        Value.Value.Integer = 6;
        TEST_CHECK_SUCCESS(run, JSONCompound_Set(Root.Value.Compound, u8"a", &Value));
        GenericBuffer_Clear(&Text);
        TEST_CHECK(run, SerializeTo(run, &Root, JSONSerializeFlags_None, &Text) && TextEquals(&Text, "{\"b\":5,\"c\":3,\"a\":6}"));

        // The key and value views follow the same order.
        const char* const Keys[] = { "b", "c", "a" };
        const int64_t Values[] = { 5, 3, 6 };
        CollectionEnumerator* KeyEnumerator = ICollection_CreateEnumerator(JSONCompound_GetKeyCollection(Root.Value.Compound));
        CollectionEnumerator* ValueEnumerator =
            ICollection_CreateEnumerator(JSONCompound_GetValueCollection(Root.Value.Compound));
        bool HasNext = false;
        size_t Index = 0U;
        while ((KeyEnumerator != NULL) && (ValueEnumerator != NULL)
            && (CollectionEnumerator_HasNext(KeyEnumerator, &HasNext).Code == ErrorCode_Success) && HasNext && (Index < 3U))
        {
            const unsigned char* Key = NULL;
            JSONObjectValue Element;
            TEST_CHECK_SUCCESS(run, CollectionEnumerator_NextByValue(KeyEnumerator, &Key));
            TEST_CHECK_SUCCESS(run, CollectionEnumerator_NextByValue(ValueEnumerator, &Element));
            TEST_CHECK(run, (Key != NULL) && (strcmp((const char*)Key, Keys[Index]) == 0));
            TEST_CHECK(run, Element.Value.Integer == Values[Index]);
            Index++;
        }
        TEST_CHECK(run, Index == 3U);
        CollectionEnumerator_Destroy(KeyEnumerator);
        CollectionEnumerator_Destroy(ValueEnumerator);

        // Clearing empties the order too.
        TEST_CHECK_SUCCESS(run, JSONCompound_Clear(Root.Value.Compound));
        TEST_CHECK_SUCCESS(run, JSONCompound_Set(Root.Value.Compound, u8"z", &Value));
        GenericBuffer_Clear(&Text);
        TEST_CHECK(run, SerializeTo(run, &Root, JSONSerializeFlags_None, &Text) && TextEquals(&Text, "{\"z\":6}"));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Root));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Text._data);
}

static void PrettyJSONKeepsScalarArraysOnOneLine(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    JSONObjectValue Root;
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool))
        && TestText_ParseJSON(run, Pool, "{\"position\": [1.5, -2, 3e2], \"tags\": [\"a\", true, null], \"empty\": [], "
            "\"objects\": [{\"id\": 1}, [2]]}", &Root))
    {
        const char* Expected =
            "{\n"
            "    \"position\": [1.5, -2, 300.0],\n"
            "    \"tags\": [\"a\", true, null],\n"
            "    \"empty\": [],\n"
            "    \"objects\": [\n"
            "        {\n"
            "            \"id\": 1\n"
            "        },\n"
            "        [2]\n"
            "    ]\n"
            "}";
        bool IsWritten = SerializeTo(run, &Root,
            (JSONSerializeFlags)(JSONSerializeFlags_PrettyPrinted | JSONSerializeFlags_InlineScalarArrays), &Text);
        TEST_CHECK(run, IsWritten && TextEquals(&Text, Expected));
        if (IsWritten && !TextEquals(&Text, Expected))
        {
            printf("      written:\n%s\n", (const char*)Text._data);
        }
        // Without the flag pretty output keeps one element per line, as before.
        GenericBuffer_Clear(&Text);
        TEST_CHECK(run, SerializeTo(run, &Root, JSONSerializeFlags_PrettyPrinted, &Text)
            && (strstr((const char*)Text._data, "\"position\": [\n        1.5,\n") != NULL));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Root));
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Text._data);
}


// Test cases: GHDF.
static void GHDFStringsReadInChunks(TestRun* run)
{
    GHDFObjectPool* Pool = NULL;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        Memory_Free(Bytes._data);
        return;
    }
    // A string over several chunks reads back whole, through a seekable stream and through a forward-only one.
    BuildStringDocument(&Bytes, FRAMEWORK_TEST_LONG_STRING_LENGTH, FRAMEWORK_TEST_LONG_STRING_LENGTH);
    for (int Pass = 0; Pass < 2; Pass++)
    {
        GHDFCompound* Root = NULL;
        if (TEST_CHECK_SUCCESS(run, ReadDocument(Pool, &Bytes, Pass == 0, &Root)))
        {
            GHDFObjectValue Entry;
            if (TEST_CHECK_SUCCESS(run, GHDFCompound_Get(Root, 1U, &Entry)))
            {
                const unsigned char* Text = Entry.Value.String->_data;
                const unsigned char* Expected = (const unsigned char*)Bytes._data + (Bytes._count
                    - FRAMEWORK_TEST_LONG_STRING_LENGTH);
                TEST_CHECK(run, (Entry.Value.String->_count == (FRAMEWORK_TEST_LONG_STRING_LENGTH + 1U))
                    && (memcmp(Text, Expected, FRAMEWORK_TEST_LONG_STRING_LENGTH) == 0)
                    && (Text[FRAMEWORK_TEST_LONG_STRING_LENGTH] == 0U));
            }
            TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Root, true));
        }
    }

    // Declared lengths the data does not hold fail before anything is reserved for them.
    struct { uint64_t Declared; size_t Present; bool IsSeekable; const char* Fragment; } Cases[] =
    {
        { (uint64_t)1 << 50U, 0U, true, "at most 1073741824 are allowed" },
        { (uint64_t)1 << 50U, 0U, false, "at most 1073741824 are allowed" },
        { GHDF_MAX_STRING_LENGTH + 1U, 3U, true, "at most 1073741824 are allowed" },
        { 1000U, 3U, true, "declares 1000 bytes, but only 3 remain" },
        // A stream that cannot tell its size is read in chunks: it ends long before the declared 1 GiB.
        { GHDF_MAX_STRING_LENGTH, 3U, false, "Unexpected end of GHDF data" },
    };
    for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
    {
        GenericBuffer_Clear(&Bytes);
        BuildStringDocument(&Bytes, Cases[Index].Declared, Cases[Index].Present);
        GHDFCompound* Root = NULL;
        const char* const Fragments[] = { Cases[Index].Fragment, NULL };
        TestText_CheckErrorMessage(run, ReadDocument(Pool, &Bytes, Cases[Index].IsSeekable, &Root), ErrorCode_Deserialize,
            Fragments);
        TEST_CHECK(run, Root == NULL);
    }
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    Memory_Free(Bytes._data);
}

static void TruncatedGHDFLeavesNothingBehind(TestRun* run)
{
    GHDFObjectPool* Pool = NULL;
    GHDFCompound* Root = NULL;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool))
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Root)))
    {
        GHDFObjectValue Big = GHDFObjectValue_CreateUInt64(5000000000ULL);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Root, 3U, GHDF_CreateRegularType(GHDFValueType_UInt64), &Big));
        GHDFObjectValue Encoded = GHDFObjectValue_CreateEncodedInteger(123456789);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Root, 900U, GHDF_CreateRegularType(GHDFValueType_EncodedInteger),
            &Encoded));
        MemoryStream Stream;
        if (TEST_CHECK_SUCCESS(run, MemoryStream_Construct1(&Stream, IOStreamFlags_CanWrite)))
        {
            TEST_CHECK_SUCCESS(run, GHDF_Write(Root, MemoryStream_AsIOStream(&Stream)));
            GenericBuffer* Written = MemoryStream_GetBuffer(&Stream);
            GenericBuffer_AppendRangeBytes(&Bytes, (unsigned char*)Written->_data, Written->_count);
            MemoryStream_Deconstruct(&Stream);
        }
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Pool, Root, true));
        // Every cut fails, and each failure frees what it made (the run's leak check counts every allocation: the
        // encoded-integer reader used to drop the stream's error without freeing it).
        for (size_t Length = 0U; Length < Bytes._count; Length++)
        {
            GenericBuffer Cut;
            GenericBuffer_CreateReadOnly(&Cut, Bytes._data, Length, sizeof(unsigned char), Length);
            GHDFCompound* Read = NULL;
            Error Result = ReadDocument(Pool, &Cut, (Length % 2U) == 0U, &Read);
            TEST_CHECK(run, (Result.Code != ErrorCode_Success) && (Read == NULL));
            Error_Deconstruct(&Result);
        }
    }
    if (Pool != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
    }
    Memory_Free(Bytes._data);
}

static void GHDFPoolsFreeContainersStillBorrowed(TestRun* run)
{
    // A compound still borrowed at teardown, holding a string and an array of compounds holding strings: the pool used
    // to release them into sub-pools it had already freed (a use-after-free the sanitizer build shows).
    GHDFObjectPool* Pool = NULL;
    if (!TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Pool)))
    {
        return;
    }
    GHDFCompound* Root = NULL;
    GHDFArray* List = NULL;
    GenericBuffer* Name = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Root))
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowArray(Pool, GHDFValueType_Compound, &List))
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowString(Pool, &Name)))
    {
        GenericBuffer_AppendString(Name, (const unsigned char*)u8"root");
        GenericBuffer_NullTerminate(Name);
        GHDFObjectValue NameValue = GHDFObjectValue_CreateString(Name);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Root, 1U, GHDF_CreateRegularType(GHDFValueType_String), &NameValue));
        for (int Index = 0; Index < 3; Index++)
        {
            GHDFCompound* Element = NULL;
            GenericBuffer* Text = NULL;
            if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Pool, &Element))
                && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowString(Pool, &Text)))
            {
                GenericBuffer_AppendString(Text, (const unsigned char*)u8"element");
                GenericBuffer_NullTerminate(Text);
                GHDFObjectValue TextValue = GHDFObjectValue_CreateString(Text);
                TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Element, 1U, GHDF_CreateRegularType(GHDFValueType_String),
                    &TextValue));
                GHDFObjectValue ElementValue = GHDFObjectValue_CreateCompound(Element);
                TEST_CHECK_SUCCESS(run, GHDFArray_AddValue(List, &ElementValue));
            }
        }
        GHDFObjectValue ListValue = GHDFObjectValue_CreateArray(List, GHDFValueType_Compound);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Root, 2U, GHDF_CreateArrayType(GHDFValueType_Compound), &ListValue));
    }
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Pool));
}


static void ReadLimitsRefuseTreesTooLarge(TestRun* run)
{
    // JSON: a thousand empty objects take about a megabyte parsed, from 3 KB of text.
    JSONObjectPool* Pool = NULL;
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer_AppendByte(&Text, (unsigned char)'[');
    for (size_t Index = 0U; Index < FRAMEWORK_TEST_LIMIT_CONTAINER_COUNT; Index++)
    {
        GenericBuffer_AppendString(&Text, (const unsigned char*)((Index > 0U) ? u8",{}" : u8"{}"));
    }
    GenericBuffer_AppendByte(&Text, (unsigned char)']');
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)))
    {
        JSONObjectValue Root;
        JSONReadLimits Small = { .MaxTreeByteCount = FRAMEWORK_TEST_SMALL_TREE_LIMIT };
        const char* const TooLarge[] = { "would take more than 100000 bytes", NULL };
        TestText_CheckErrorMessage(run, JSON_DeserializeWithLimits(Pool, &Text, &Small, &Root), ErrorCode_InvalidJSON,
            TooLarge);
        TEST_CHECK(run, Root.Type == JSONValueType_None);
        JSONReadLimits Large = { .MaxTreeByteCount = FRAMEWORK_TEST_LARGE_TREE_LIMIT };
        if (TEST_CHECK_SUCCESS(run, JSON_DeserializeWithLimits(Pool, &Text, &Large, &Root)))
        {
            TEST_CHECK(run, (Root.Type == JSONValueType_Array)
                && (JSONArray_GetElementCount(Root.Value.Array) == FRAMEWORK_TEST_LIMIT_CONTAINER_COUNT));
            TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Root));
        }

        // A string is charged twice its length (its buffer grows in powers of two): 60 000 bytes pass a 100 000 limit.
        GenericBuffer_Clear(&Text);
        GenericBuffer_AppendByte(&Text, (unsigned char)'"');
        for (size_t Index = 0U; Index < 60000U; Index++)
        {
            GenericBuffer_AppendByte(&Text, (unsigned char)'a');
        }
        GenericBuffer_AppendByte(&Text, (unsigned char)'"');
        TestText_CheckErrorMessage(run, JSON_DeserializeWithLimits(Pool, &Text, &Small, &Root), ErrorCode_InvalidJSON,
            TooLarge);
        TEST_CHECK_ERROR(run, JSON_DeserializeWithLimits(Pool, &Text, NULL, &Root), ErrorCode_IllegalArgument);
        JSONReadLimits Zero = { .MaxTreeByteCount = 0U };
        TEST_CHECK_ERROR(run, JSON_DeserializeWithLimits(Pool, &Text, &Zero, &Root), ErrorCode_ArgumentOutOfRange);
        // The partial trees went back to the pool: the run's leak check counts every allocation.
        TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
    }

    // GHDF: an empty compound in an array of compounds is one byte of data.
    GHDFObjectPool* Writer = NULL;
    GHDFCompound* Document = NULL;
    GHDFArray* Compounds = NULL;
    GenericBuffer_Clear(&Text);
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Writer))
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Writer, &Document))
        && TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowArray(Writer, GHDFValueType_Compound, &Compounds)))
    {
        for (size_t Index = 0U; Index < FRAMEWORK_TEST_LIMIT_CONTAINER_COUNT; Index++)
        {
            GHDFCompound* Element = NULL;
            if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(Writer, &Element)))
            {
                GHDFObjectValue ElementValue = GHDFObjectValue_CreateCompound(Element);
                TEST_CHECK_SUCCESS(run, GHDFArray_AddValue(Compounds, &ElementValue));
            }
        }
        GHDFObjectValue ArrayValue = GHDFObjectValue_CreateArray(Compounds, GHDFValueType_Compound);
        TEST_CHECK_SUCCESS(run, GHDFCompound_SetValue(Document, 1U, GHDF_CreateArrayType(GHDFValueType_Compound),
            &ArrayValue));
        MemoryStream Out;
        if (TEST_CHECK_SUCCESS(run, MemoryStream_Construct1(&Out, IOStreamFlags_CanWrite)))
        {
            TEST_CHECK_SUCCESS(run, GHDF_Write(Document, MemoryStream_AsIOStream(&Out)));
            GenericBuffer* Written = MemoryStream_GetBuffer(&Out);
            GenericBuffer_AppendRangeBytes(&Text, (unsigned char*)Written->_data, Written->_count);
            MemoryStream_Deconstruct(&Out);
        }
    }
    if (Writer != NULL)
    {
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Writer));
    }
    GHDFObjectPool* Reader = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&Reader)))
    {
        struct { size_t Limit; bool IsRefused; } Cases[] =
        {
            { FRAMEWORK_TEST_SMALL_TREE_LIMIT, true }, { FRAMEWORK_TEST_LARGE_TREE_LIMIT, false }
        };
        for (size_t Index = 0U; Index < (sizeof(Cases) / sizeof(Cases[0])); Index++)
        {
            GenericBuffer Source;
            GenericBuffer_CreateReadOnly(&Source, Text._data, Text._count, sizeof(unsigned char), Text._count);
            MemoryStream In;
            if (!TEST_CHECK_SUCCESS(run, MemoryStream_Construct2(&In, &Source, IOStreamFlags_CanRead)))
            {
                continue;
            }
            GHDFReadLimits Limits = { .MaxTreeByteCount = Cases[Index].Limit };
            GHDFCompound* Root = NULL;
            Error Result = GHDF_ReadWithLimits(MemoryStream_AsIOStream(&In), Reader, &Limits, &Root);
            if (Cases[Index].IsRefused)
            {
                const char* const TooLarge[] = { "would take more than 100000 bytes", NULL };
                TestText_CheckErrorMessage(run, Result, ErrorCode_Deserialize, TooLarge);
                TEST_CHECK(run, Root == NULL);
            }
            else if (TEST_CHECK_SUCCESS(run, Result))
            {
                TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(Reader, Root, true));
            }
            MemoryStream_Deconstruct(&In);
        }
        TEST_CHECK_ERROR(run, GHDF_ReadWithLimits(NULL, Reader, NULL, NULL), ErrorCode_IllegalArgument);
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(Reader));
    }
    Memory_Free(Text._data);
}


// Test cases: sorting.
static void SortsTakeSortedReversedAndEqualInput(TestRun* run)
{
    uint32_t State = 2463534242U;
    for (int Pattern = 0; Pattern < (int)SortPattern_Count; Pattern++)
    {
        CheckSorts(run, (SortPattern)Pattern, FRAMEWORK_TEST_SORT_COUNT, &State);
    }
    // Every short length, where the partitioning hands over to insertion sort.
    for (size_t Length = 0U; Length <= FRAMEWORK_TEST_SMALL_SORT_LENGTH; Length++)
    {
        CheckSorts(run, SortPattern_FewDistinct, Length, &State);
        CheckSorts(run, SortPattern_Random, Length, &State);
        CheckSorts(run, SortPattern_Descending, Length, &State);
    }
}


// Test cases: object pools.
static void ObjectPoolsReuseSlotsAndFindObjectsByAddress(TestRun* run)
{
    const size_t ElementSize = 24U;
    PoolCounts Counts = { 0U, 0U };
    ObjectPoolLifecycle Lifecycle = { .ConstructObject = CountConstructedObject, .ResetObject = NULL,
        .DeconstructObject = CountDeconstructedObject };
    UserData Context = UserData_FromPointer(&Counts);
    ObjectPool Pool;
    if (!TEST_CHECK_SUCCESS(run, ObjectPool_Construct2(&Pool, ElementSize, 4U, Lifecycle, &Context)))
    {
        return;
    }
    GenericBuffer Objects;
    GenericBuffer_AllocateVariable(&Objects, FRAMEWORK_TEST_POOL_COUNT, sizeof(void*));
    bool IsEveryBorrowClean = true;
    for (size_t Index = 0U; (Index < FRAMEWORK_TEST_POOL_COUNT) && IsEveryBorrowClean; Index++)
    {
        void* Object = NULL;
        Error Result = ObjectPool_GetNewObject(&Pool, &Object);
        IsEveryBorrowClean = (Result.Code == ErrorCode_Success) && IsZero(Object, ElementSize);
        Error_Deconstruct(&Result);
        if (IsEveryBorrowClean)
        {
            memset(Object, 0xAB, ElementSize);
            GenericBuffer_AddLast(&Objects, &Object);
        }
    }
    TEST_CHECK(run, IsEveryBorrowClean && (Counts.Constructed == FRAMEWORK_TEST_POOL_COUNT));
    TEST_CHECK(run, AreObjectsSeparate(&Objects, ElementSize));

    // Pointers the pool did not hand out are refused; returning a free slot again changes nothing.
    unsigned char Foreign[24];
    void* First = ((void**)Objects._data)[0];
    TEST_CHECK_ERROR(run, ObjectPool_DisposeObject(&Pool, Foreign), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, ObjectPool_DisposeObject(&Pool, (unsigned char*)First + 1), ErrorCode_IllegalArgument);
    TEST_CHECK_SUCCESS(run, ObjectPool_DisposeObject(&Pool, First));
    TEST_CHECK_SUCCESS(run, ObjectPool_DisposeObject(&Pool, First));
    void* Again = NULL;
    if (TEST_CHECK_SUCCESS(run, ObjectPool_GetNewObject(&Pool, &Again)))
    {
        ((void**)Objects._data)[0] = Again;
    }

    // Every other object back, in an order unrelated to the addresses; borrowed again, they come back zeroed and new
    // slots are made only for what the free slots cannot cover.
    uint32_t State = 88675123U;
    void** Live = (void**)Objects._data;
    for (size_t Index = Objects._count - 1U; Index > 0U; Index--)
    {
        size_t Pick = NextRandom(&State) % (Index + 1U);
        void* Swap = Live[Index];
        Live[Index] = Live[Pick];
        Live[Pick] = Swap;
    }
    size_t Returned = 0U;
    bool IsEveryReturnTaken = true;
    for (size_t Index = 0U; Index < Objects._count; Index += 2U)
    {
        Error Result = ObjectPool_DisposeObject(&Pool, Live[Index]);
        IsEveryReturnTaken = IsEveryReturnTaken && (Result.Code == ErrorCode_Success);
        Error_Deconstruct(&Result);
        Returned++;
    }
    TEST_CHECK(run, IsEveryReturnTaken);
    size_t ConstructedBefore = Counts.Constructed;
    IsEveryBorrowClean = true;
    for (size_t Index = 0U; (Index < Returned) && IsEveryBorrowClean; Index++)
    {
        void* Object = NULL;
        Error Result = ObjectPool_GetNewObject(&Pool, &Object);
        IsEveryBorrowClean = (Result.Code == ErrorCode_Success) && IsZero(Object, ElementSize);
        Error_Deconstruct(&Result);
        Live[Index * 2U] = Object;
    }
    TEST_CHECK(run, IsEveryBorrowClean && (Counts.Constructed == ConstructedBefore));
    TEST_CHECK(run, AreObjectsSeparate(&Objects, ElementSize));

    // A clear frees every slot at once: borrowing as many again makes no new slot.
    TEST_CHECK_SUCCESS(run, ObjectPool_Clear(&Pool));
    bool IsEveryBorrowMade = true;
    for (size_t Index = 0U; (Index < FRAMEWORK_TEST_POOL_COUNT) && IsEveryBorrowMade; Index++)
    {
        void* Object = NULL;
        Error Result = ObjectPool_GetNewObject(&Pool, &Object);
        IsEveryBorrowMade = Result.Code == ErrorCode_Success;
        Error_Deconstruct(&Result);
    }
    TEST_CHECK(run, IsEveryBorrowMade && (Counts.Constructed == ConstructedBefore));
    TEST_CHECK_SUCCESS(run, ObjectPool_Deconstruct(&Pool));
    TEST_CHECK(run, Counts.Deconstructed == Counts.Constructed);
    Memory_Free(Objects._data);
}


// Test cases: files.
static void ReplaceEntryReplacesAFileInOneStep(TestRun* run)
{
    GenericBuffer Directory;
    GenericBuffer_AllocateVariable(&Directory, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Target;
    GenericBuffer_AllocateVariable(&Target, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Source;
    GenericBuffer_AllocateVariable(&Source, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Fresh;
    GenericBuffer_AllocateVariable(&Fresh, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, TestRun_CreateWorkDirectory(run, "replace_entry", &Directory))
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Directory._data, "target.txt", "old"))
        && TEST_CHECK_SUCCESS(run, Test_WriteTextFile(Directory._data, "target.txt.tmp", "new"))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, (const unsigned char*)u8"target.txt", &Target))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, (const unsigned char*)u8"target.txt.tmp", &Source))
        && TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, (const unsigned char*)u8"fresh.txt", &Fresh)))
    {
        // An existing file is replaced, and the source is gone.
        TEST_CHECK_SUCCESS(run, FileSystem_ReplaceEntry(Source._data, Target._data));
        TEST_CHECK(run, (FileSystem_ReadAllBytes(Target._data, &Text).Code == ErrorCode_Success) && (Text._count == 3U)
            && (memcmp(Text._data, "new", 3U) == 0));
        size_t FileCount = 0U;
        TEST_CHECK_SUCCESS(run, Test_CountFiles(Directory._data, &FileCount));
        TEST_CHECK(run, FileCount == 1U);
        // A destination that does not exist yet is fine; a missing source is not.
        TEST_CHECK_SUCCESS(run, FileSystem_ReplaceEntry(Target._data, Fresh._data));
        Error Missing = FileSystem_ReplaceEntry(Target._data, Fresh._data);
        TEST_CHECK(run, Missing.Code != ErrorCode_Success);
        Error_Deconstruct(&Missing);
        TEST_CHECK_ERROR(run, FileSystem_ReplaceEntry(NULL, Fresh._data), ErrorCode_IllegalArgument);

        // A directory is no file: it stays where it is (rename() alone would move it to a missing path).
        GenericBuffer Folder;
        GenericBuffer_AllocateVariable(&Folder, FRAMEWORK_TEST_TEXT_CAPACITY, sizeof(unsigned char));
        if (TEST_CHECK_SUCCESS(run, Path_Append(Directory._data, (const unsigned char*)u8"folder", &Folder))
            && TEST_CHECK_SUCCESS(run, FileSystem_CreateDirectory(Folder._data)))
        {
            TEST_CHECK_ERROR(run, FileSystem_ReplaceEntry(Folder._data, Target._data), ErrorCode_IO);
            FileSystemEntryInfo Info;
            if (TEST_CHECK_SUCCESS(run, FileSystem_GetEntryInfo(Folder._data, &Info)))
            {
                TEST_CHECK(run, Info._entryType == FileSystemEntryType_Directory);
                FileSystemEntryInfo_Deconstruct(&Info);
            }
        }
        Memory_Free(Folder._data);
    }
    Memory_Free(Text._data);
    Memory_Free(Fresh._data);
    Memory_Free(Source._data);
    Memory_Free(Target._data);
    Memory_Free(Directory._data);
}


// Fields: the suite.
static const TestCase WRFrameworkTestCases[] =
{
    { "json_compounds_keep_insertion_order", JSONCompoundsKeepInsertionOrder },
    { "pretty_json_keeps_scalar_arrays_on_one_line", PrettyJSONKeepsScalarArraysOnOneLine },
    { "ghdf_strings_read_in_chunks", GHDFStringsReadInChunks },
    { "truncated_ghdf_leaves_nothing_behind", TruncatedGHDFLeavesNothingBehind },
    { "ghdf_pools_free_containers_still_borrowed", GHDFPoolsFreeContainersStillBorrowed },
    { "read_limits_refuse_trees_too_large", ReadLimitsRefuseTreesTooLarge },
    { "sorts_take_sorted_reversed_and_equal_input", SortsTakeSortedReversedAndEqualInput },
    { "object_pools_reuse_slots_and_find_objects_by_address", ObjectPoolsReuseSlotsAndFindObjectsByAddress },
    { "replace_entry_replaces_a_file_in_one_step", ReplaceEntryReplacesAFileInOneStep },
};


// Public functions.
void WRFrameworkTests_Run(TestRun* run)
{
    TestRun_RunSuite(run, "WRFramework", WRFrameworkTestCases, sizeof(WRFrameworkTestCases) / sizeof(WRFrameworkTestCases[0]));
}
