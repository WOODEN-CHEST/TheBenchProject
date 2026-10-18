#include <inttypes.h>
#include "FontCodepoints.h"
#include "GameJSON.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/* Smallest codepoint a font may request (U+0020, space); control characters are never drawn. */
#define FONT_CODEPOINT_MIN ((int64_t)0x20)
#define FONT_CODEPOINT_MAX ((int64_t)0x10FFFF)
#define FONT_SURROGATE_FIRST ((int64_t)0xD800)
#define FONT_SURROGATE_LAST ((int64_t)0xDFFF)
/* Always rasterized: the glyph drawn for every codepoint a font lacks, and the space (its advance spaces words). */
#define FONT_FALLBACK_CODEPOINT ((int64_t)'?')
#define FONT_SPACE_CODEPOINT ((int64_t)' ')


// Types.
typedef struct CodepointRangeStruct
{
    int64_t First;
    int64_t Last;
} CodepointRange;

typedef struct CodepointBlockStruct
{
    const unsigned char* Name;
    int64_t First;
    int64_t Last;
} CodepointBlock;


// Fields.
/* Named Unicode blocks a definition may list; only blocks whose every codepoint is assigned, so a font that covers
 * the script really has all of them (other ranges are given as numbers). */
static const CodepointBlock NamedBlocks[] =
{
    { u8"basic_latin", 0x20, 0x7E },
    { u8"latin_1_supplement", 0xA0, 0xFF },
    { u8"latin_extended_a", 0x100, 0x17F },
    { u8"latin_extended_b", 0x180, 0x24F },
    { u8"cyrillic", 0x400, 0x4FF }
};

/* What a definition without "codepoints" rasterizes: English plus the Latin letters of most European languages
 * (Latvian included). */
static const CodepointRange DefaultRanges[] =
{
    { 0x20, 0x7E },
    { 0xA0, 0xFF },
    { 0x100, 0x17F }
};


// Static functions.
static bool AddRange(GenericBuffer* ranges, int64_t first, int64_t last)
{
    CodepointRange Range = { .First = first, .Last = last };
    return GenericBuffer_AddLast(ranges, &Range);
}

static Error ValidateRange(int64_t first, int64_t last, size_t entryIndex)
{
    bool IsInBounds = (first >= FONT_CODEPOINT_MIN) && (last <= FONT_CODEPOINT_MAX) && (first <= last);
    bool HasSurrogates = (first <= FONT_SURROGATE_LAST) && (last >= FONT_SURROGATE_FIRST);
    if (!IsInBounds || HasSurrogates)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"\"codepoints\" entry %" PRIu64 u8" must be within U+0020-U+10FFFF, first <= last, and must not include "
            u8"the surrogates U+D800-U+DFFF.", (uint64_t)entryIndex);
    }
    return Error_CreateSuccess();
}

static Error ReadCodepointNumber(const JSONObjectValue* value, size_t entryIndex, int64_t* outCodepoint)
{
    Error Result = GameJSON_ValueToInt64(value, outCodepoint);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"\"codepoints\" entry %" PRIu64 u8" must hold integer codepoints (numbers, or strings such as \"0x400\").",
            (uint64_t)entryIndex);
    }
    return Error_CreateSuccess();
}

static Error ParseBlockName(const JSONObjectValue* entry, size_t entryIndex, GenericBuffer* ranges)
{
    GenericBuffer* Name = entry->Value.String;
    size_t Length = (Name != NULL) ? Name->_count : 0U;
    for (size_t Index = 0; Index < (sizeof(NamedBlocks) / sizeof(NamedBlocks[0])); Index++)
    {
        const CodepointBlock* Block = &NamedBlocks[Index];
        if ((StringUTF8_GetByteLength(Block->Name) == Length) && (Length > 0U) && Memory_IsEqual(Block->Name, Name->_data, Length))
        {
            if (!AddRange(ranges, Block->First, Block->Last))
            {
                return Error_Construct2(ErrorCode_BufferTooLarge, "Could not store a codepoint range.");
            }
            return Error_CreateSuccess();
        }
    }
    return Error_Construct3(ErrorCode_InvalidAssetDefinition,
        u8"\"codepoints\" entry %" PRIu64 u8" names no known block; the blocks are basic_latin, latin_1_supplement, "
        u8"latin_extended_a, latin_extended_b and cyrillic (give other ranges as [first, last]).", (uint64_t)entryIndex);
}

// An entry is a block name, one codepoint, or a [first, last] range.
static Error ParseCodepointEntry(const JSONObjectValue* entry, size_t entryIndex, GenericBuffer* ranges)
{
    if (entry->Type == JSONValueType_String)
    {
        return ParseBlockName(entry, entryIndex, ranges);
    }

    int64_t First = 0;
    int64_t Last = 0;
    Error Result = Error_CreateSuccess();
    if ((entry->Type == JSONValueType_Integer) || (entry->Type == JSONValueType_RealNumber))
    {
        Result = ReadCodepointNumber(entry, entryIndex, &First);
        Last = First;
    }
    else if ((entry->Type == JSONValueType_Array) && (JSONArray_GetElementCount(entry->Value.Array) == 2U))
    {
        JSONObjectValue Bound;
        Result = JSONArray_Get(entry->Value.Array, 0U, &Bound);
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadCodepointNumber(&Bound, entryIndex, &First);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = JSONArray_Get(entry->Value.Array, 1U, &Bound);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = ReadCodepointNumber(&Bound, entryIndex, &Last);
        }
    }
    else
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"\"codepoints\" entry %" PRIu64 u8" must be a block name, a codepoint or a [first, last] range.",
            (uint64_t)entryIndex);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = ValidateRange(First, Last, entryIndex);
    if ((Result.Code == ErrorCode_Success) && !AddRange(ranges, First, Last))
    {
        Result = Error_Construct2(ErrorCode_BufferTooLarge, "Could not store a codepoint range.");
    }
    return Result;
}

// Sorts the ranges by their first codepoint (insertion sort: definitions list a handful) and merges overlapping and
// adjacent ones, returning the number of codepoints they cover.
static int64_t SortAndMergeRanges(GenericBuffer* ranges)
{
    CodepointRange* Items = (ranges->_count > 0U) ? GenericBuffer_GetPointerToElement(ranges, 0U) : NULL;
    size_t Count = ranges->_count;
    for (size_t Index = 1U; Index < Count; Index++)
    {
        CodepointRange Current = Items[Index];
        size_t Position = Index;
        while ((Position > 0U) && (Items[Position - 1U].First > Current.First))
        {
            Items[Position] = Items[Position - 1U];
            Position--;
        }
        Items[Position] = Current;
    }

    size_t MergedCount = 0U;
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if ((MergedCount > 0U) && (Items[Index].First <= (Items[MergedCount - 1U].Last + 1)))
        {
            if (Items[Index].Last > Items[MergedCount - 1U].Last)
            {
                Items[MergedCount - 1U].Last = Items[Index].Last;
            }
            continue;
        }
        Items[MergedCount] = Items[Index];
        MergedCount++;
    }
    GenericBuffer_SetCount(ranges, MergedCount);

    int64_t Total = 0;
    for (size_t Index = 0U; Index < MergedCount; Index++)
    {
        Total += (Items[Index].Last - Items[Index].First) + 1;
    }
    return Total;
}

// Collects the ranges the definition asks for (the default set when "codepoints" is absent), plus '?' and ' '.
static Error CollectRanges(JSONCompound* root, GenericBuffer* ranges)
{
    JSONObjectValue Value;
    bool IsFound = false;
    Error Result = JSONCompound_GetOptional(root, (const unsigned char*)u8"codepoints", &Value, &IsFound);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!IsFound)
    {
        for (size_t Index = 0U; Index < (sizeof(DefaultRanges) / sizeof(DefaultRanges[0])); Index++)
        {
            if (!AddRange(ranges, DefaultRanges[Index].First, DefaultRanges[Index].Last))
            {
                return Error_Construct2(ErrorCode_BufferTooLarge, "Could not store a codepoint range.");
            }
        }
    }
    else if (Value.Type != JSONValueType_Array)
    {
        return Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"\"codepoints\" must be an array of block names, codepoints and [first, last] ranges.");
    }
    else
    {
        // Bounded before anything is read: merging sorts the entries in quadratic time.
        size_t EntryCount = JSONArray_GetElementCount(Value.Value.Array);
        if (EntryCount > FONT_CODEPOINTS_MAX_ENTRY_COUNT)
        {
            return Error_Construct3(ErrorCode_InvalidAssetDefinition,
                u8"\"codepoints\" has %" PRIu64 u8" entries; at most %" PRIu64 u8" are allowed.", (uint64_t)EntryCount,
                (uint64_t)FONT_CODEPOINTS_MAX_ENTRY_COUNT);
        }
        for (size_t Index = 0U; Index < EntryCount; Index++)
        {
            JSONObjectValue Entry;
            Result = JSONArray_Get(Value.Value.Array, Index, &Entry);
            if (Result.Code == ErrorCode_Success)
            {
                Result = ParseCodepointEntry(&Entry, Index, ranges);
            }
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }

    if (!AddRange(ranges, FONT_FALLBACK_CODEPOINT, FONT_FALLBACK_CODEPOINT)
        || !AddRange(ranges, FONT_SPACE_CODEPOINT, FONT_SPACE_CODEPOINT))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "Could not store a codepoint range.");
    }
    return Error_CreateSuccess();
}

// Public functions.
Error FontCodepoints_Read(JSONCompound* root, int pixelSize, int** outCodepoints, int* outCount)
{
    *outCodepoints = NULL;
    *outCount = 0;

    GenericBuffer Ranges;
    GenericBuffer_AllocateVariable(&Ranges, 8U, sizeof(CodepointRange));

    Error Result = CollectRanges(root, &Ranges);
    int64_t Total = (Result.Code == ErrorCode_Success) ? SortAndMergeRanges(&Ranges) : 0;
    if ((Result.Code == ErrorCode_Success) && (Total > FONT_CODEPOINTS_MAX))
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"\"codepoints\" covers %" PRId64 u8" codepoints; at most %" PRId64 u8" are allowed.", Total, FONT_CODEPOINTS_MAX);
    }
    if ((Result.Code == ErrorCode_Success)
        && (((uint64_t)Total * (uint64_t)pixelSize * (uint64_t)pixelSize) > FONT_CODEPOINTS_MAX_ATLAS_AREA))
    {
        Result = Error_Construct3(ErrorCode_InvalidAssetDefinition,
            u8"%" PRId64 u8" codepoints at size %d would not fit an 8192 x 8192 atlas; lower \"size\" or the codepoints.",
            Total, pixelSize);
    }
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Ranges._data);
        return Result;
    }

    int* Codepoints = Memory_Allocate((size_t)Total * sizeof(int));
    size_t Next = 0U;
    for (size_t Index = 0U; Index < Ranges._count; Index++)
    {
        const CodepointRange* Range = GenericBuffer_GetPointerToElement(&Ranges, Index);
        for (int64_t Codepoint = Range->First; Codepoint <= Range->Last; Codepoint++)
        {
            Codepoints[Next] = (int)Codepoint;
            Next++;
        }
    }
    Memory_Free(Ranges._data);
    *outCodepoints = Codepoints;
    *outCount = (int)Total;
    return Error_CreateSuccess();
}
