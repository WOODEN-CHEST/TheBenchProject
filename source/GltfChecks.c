#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "GltfChecks.h"
#include "wr/WRFileSystem.h"
#include "wr/WRMemory.h"

/*
 * Reading hostile glTF files over cgltf (source/GltfChecks.h). Moved here from the animation library's reader (T29) when
 * the terrain's import (B01) needed the same checks; the messages are the ones that reader gave.
 *
 * WHAT CGLTF MAY ALLOCATE. cgltf's parse allocates every JSON array's elements (and its token list) before any check
 * here can run, so a small file listing millions of empty objects would take gigabytes, and Memory_Allocate aborts the
 * game when memory runs out. Every allocation cgltf makes goes through one read's budget instead: the parse may take
 * PARSE_BUDGET_BYTES plus the file's size (the strings it copies out of it), the buffers' load the reader's limit on top
 * of that (CheckBuffers holds the buffers' declared lengths to it). Past the budget cgltf is told the memory ran out, and
 * the read fails with a message.
 */


// Macros.
/** Room for one message's text and for a name from the file in printable form. */
#define MESSAGE_CAPACITY ((size_t)512)
#define NAME_PRINT_CAPACITY ((size_t)129)
/** What cgltf's parse may allocate beyond the file's own size (its tokens and the parsed objects), in bytes. */
#define PARSE_BUDGET_BYTES ((size_t)64U * 1024U * 1024U)
/** Bytes in a mebibyte, for messages. */
#define MEBIBYTE ((size_t)1024U * 1024U)
/** glTF's limits on a buffer view's stride, in bytes between elements (0: tightly packed): 4 to 252, in fours. */
#define MIN_VIEW_STRIDE ((cgltf_size)4)
#define MAX_VIEW_STRIDE ((cgltf_size)252)
#define VIEW_STRIDE_STEP ((cgltf_size)4)
/** The alignment a buffer's data must have, so cgltf's float and integer reads are aligned (glTF pads to four). */
#define BUFFER_DATA_ALIGNMENT ((uintptr_t)4)
/** Bytes a data URI's base64 holds for every four of its characters (and a group a partial one may add). */
#define BASE64_BYTES_PER_GROUP ((size_t)3)
#define BASE64_CHARACTERS_PER_GROUP ((size_t)4)


// Types.
/* What one read's cgltf calls may still allocate (the file comment): every allocation is counted, frees are not (the
 * count bounds the peak). */
typedef struct CgltfBudgetStruct
{
    size_t Used;
    size_t Limit;
    bool IsExceeded;
} CgltfBudget;


// Static functions: cgltf's memory and files.
/* The sum of two sizes, or SIZE_MAX when it would not fit. */
static size_t AddOrSaturate(size_t a, size_t b)
{
    return (a > (SIZE_MAX - b)) ? SIZE_MAX : (a + b);
}

/* cgltf's allocator: Memory_Allocate within the read's budget (user data; NULL: none), NULL past it. */
static void* AllocateForCgltf(void* user, cgltf_size size)
{
    CgltfBudget* Budget = user;
    size_t Size = (size > 0U) ? (size_t)size : 1U;
    if (Budget != NULL)
    {
        if ((Size > Budget->Limit) || (Budget->Used > (Budget->Limit - Size)))
        {
            Budget->IsExceeded = true;
            return NULL;
        }
        Budget->Used += Size;
    }
    return Memory_Allocate(Size);
}

static void FreeForCgltf(void* user, void* pointer)
{
    (void)user;
    Memory_Free(pointer);
}

/* cgltf's file reader for a buffer in a file of its own, through WRFileSystem (UTF-8 paths on every platform). cgltf
 * asks for the buffer's declared length (0: any) and checks every view against that length, so, as its own reader does,
 * a file shorter than it is refused and only the declared bytes are kept. */
static cgltf_result ReadBufferFileForCgltf(const cgltf_memory_options* memory, const cgltf_file_options* file,
    const char* path, cgltf_size* size, void** data)
{
    // The reader's limit rides in the file options' user data.
    const size_t* Limit = file->user_data;
    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    Error Result = GltfChecks_ReadLimitedFile((const unsigned char*)path, *Limit, &Bytes);
    if (Result.Code != ErrorCode_Success)
    {
        cgltf_result Answer = cgltf_result_io_error;
        if (Result.Code == ErrorCode_BufferTooLarge)
        {
            Answer = cgltf_result_data_too_short;
        }
        else if ((Result.Code == ErrorCode_FileNotFound) || (Result.Code == ErrorCode_DirectoryNotFound))
        {
            Answer = cgltf_result_file_not_found;
        }
        Error_Deconstruct(&Result);
        Memory_Free(Bytes._data);
        return Answer;
    }
    size_t Wanted = (*size > 0U) ? (size_t)*size : Bytes._count;
    if (Bytes._count < Wanted)
    {
        Memory_Free(Bytes._data);
        return cgltf_result_data_too_short;
    }
    void* Copy = memory->alloc_func(memory->user_data, (Wanted > 0U) ? Wanted : 1U);
    if (Copy == NULL)
    {
        Memory_Free(Bytes._data);
        return cgltf_result_out_of_memory;
    }
    if (Wanted > 0U)
    {
        Memory_Copy(Bytes._data, Copy, Wanted);
    }
    *size = Wanted;
    *data = Copy;
    Memory_Free(Bytes._data);
    return cgltf_result_success;
}

static void ReleaseBufferFileForCgltf(const cgltf_memory_options* memory, const cgltf_file_options* file, void* data,
    cgltf_size size)
{
    (void)file;
    (void)size;
    memory->free_func(memory->user_data, data);
}

/* A decoded buffer path stays next to the library: relative, no ".." segment, no drive or backslash. */
static bool IsBufferPathSafe(const char* uri)
{
    if ((uri[0] == '/') || (strchr(uri, '\\') != NULL) || (strchr(uri, ':') != NULL))
    {
        return false;
    }
    const char* Segment = uri;
    while (true)
    {
        const char* Slash = strchr(Segment, '/');
        size_t Length = (Slash != NULL) ? (size_t)(Slash - Segment) : strlen(Segment);
        if ((Length == 2U) && (Segment[0] == '.') && (Segment[1] == '.'))
        {
            return false;
        }
        if (Slash == NULL)
        {
            return true;
        }
        Segment = Slash + 1;
    }
}

/* A buffer file's URI is checked as cgltf opens it: with its percent escapes decoded (so "%2e%2e/" is ".."), and with no
 * escaped NUL cutting it short. */
static bool IsBufferUriSafe(const char* uri)
{
    size_t Length = strlen(uri);
    char* Decoded = Memory_Allocate(Length + 1U);
    Memory_Copy(uri, Decoded, Length + 1U);
    size_t DecodedLength = cgltf_decode_uri(Decoded);
    bool IsSafe = (strlen(Decoded) == DecodedLength) && IsBufferPathSafe(Decoded);
    Memory_Free(Decoded);
    return IsSafe;
}

static const char* DescribeCgltfResult(cgltf_result result)
{
    switch (result)
    {
        case cgltf_result_success: return "no error";
        case cgltf_result_data_too_short: return "its data is too short";
        case cgltf_result_unknown_format: return "it is not glTF (or names a buffer it cannot read)";
        case cgltf_result_invalid_json: return "its JSON is invalid";
        case cgltf_result_invalid_gltf: return "it breaks the glTF rules";
        case cgltf_result_invalid_options: return "the reader's options are invalid";
        case cgltf_result_file_not_found: return "a buffer file is missing";
        case cgltf_result_io_error: return "a buffer file cannot be read";
        case cgltf_result_out_of_memory: return "it needs too much memory";
        case cgltf_result_legacy_gltf: return "it is glTF 1.0, not 2.0";
        case cgltf_result_max_enum: return "an unknown error";
    }
    return "an unknown error";
}


// Static functions: the structure.
/* An ErrorCode_InvalidAssetData error: "<description>: <formatted text>". */
static Error Invalid(const GltfReadContext* context, const char* format, ...)
{
    char Text[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    return Error_Construct4(ErrorCode_InvalidAssetData, "%s: %s", context->Description, Text);
}

/* The buffers, before cgltf loads them: each declares at least a byte, together at most the file limit, and a data URI
 * no more than its base64 holds (cgltf allocates the declared length before it decodes). */
static Error CheckBuffers(const GltfReadContext* context, const cgltf_data* data)
{
    const cgltf_data* Data = data;
    size_t TotalBytes = 0U;
    for (cgltf_size Index = 0U; Index < Data->buffers_count; Index++)
    {
        const cgltf_buffer* Buffer = &Data->buffers[Index];
        if ((Buffer->size == 0U) || (Buffer->size > (context->MaxBytes - TotalBytes)))
        {
            return Invalid(context, "buffer %" PRIu64 " declares %" PRIu64 " bytes; the "
                "buffers hold 1 to %" PRIu64 " bytes together.", (uint64_t)Index, (uint64_t)Buffer->size,
                (uint64_t)context->MaxBytes);
        }
        TotalBytes += Buffer->size;
        if ((Buffer->uri != NULL) && (strncmp(Buffer->uri, "data:", 5U) == 0))
        {
            const char* Comma = strchr(Buffer->uri, ',');
            size_t Characters = (Comma != NULL) ? strlen(Comma + 1) : 0U;
            size_t MostBytes = ((Characters / BASE64_CHARACTERS_PER_GROUP) + 1U) * BASE64_BYTES_PER_GROUP;
            if (Buffer->size > MostBytes)
            {
                return Invalid(context, "buffer %" PRIu64 " declares %" PRIu64 " bytes, more "
                    "than its data holds.", (uint64_t)Index, (uint64_t)Buffer->size);
            }
        }
    }
    return Error_CreateSuccess();
}

/* The buffer views: each within its buffer, its stride glTF's (4 to 252 bytes, in fours, or packed), not compressed. */
static Error CheckBufferViews(const GltfReadContext* context, const cgltf_data* data)
{
    const cgltf_data* Data = data;
    for (cgltf_size Index = 0U; Index < Data->buffer_views_count; Index++)
    {
        const cgltf_buffer_view* View = &Data->buffer_views[Index];
        if ((View->buffer == NULL) || (View->size == 0U) || (View->size > View->buffer->size)
            || (View->offset > (View->buffer->size - View->size)))
        {
            return Invalid(context, "buffer view %" PRIu64 " (%" PRIu64 " bytes from %" PRIu64
                ") does not fit in its buffer.", (uint64_t)Index, (uint64_t)View->size, (uint64_t)View->offset);
        }
        if ((View->stride != 0U) && ((View->stride < MIN_VIEW_STRIDE) || (View->stride > MAX_VIEW_STRIDE)
            || ((View->stride % VIEW_STRIDE_STEP) != 0U)))
        {
            return Invalid(context, "buffer view %" PRIu64 " has a stride of %" PRIu64 " bytes; "
                "glTF allows %" PRIu64 " to %" PRIu64 ", in fours.", (uint64_t)Index, (uint64_t)View->stride,
                (uint64_t)MIN_VIEW_STRIDE, (uint64_t)MAX_VIEW_STRIDE);
        }
        if (View->has_meshopt_compression)
        {
            return Invalid(context, "buffer view %" PRIu64 " is meshopt-compressed, which the "
                "game does not read.", (uint64_t)Index);
        }
    }
    return Error_CreateSuccess();
}

/* Whether @p count packed elements of @p elementSize bytes from @p offset fit in a view (reckoned without overflow). */
static bool FitsInView(const cgltf_buffer_view* view, cgltf_size offset, cgltf_size elementSize, cgltf_size count)
{
    return (view != NULL) && (elementSize > 0U) && (count > 0U) && (offset <= view->size)
        && (count <= ((view->size - offset) / elementSize));
}

/* A sparse accessor's replacements (glTF's rules, which bound every sum and loop cgltf makes over them): 1 to the
 * accessor's count of them, indices of an unsigned integer type, indices and values packed inside their views and
 * aligned to their components. */
static Error CheckSparse(const GltfReadContext* context, cgltf_size index, const cgltf_accessor* accessor,
    cgltf_size elementSize, cgltf_size componentSize)
{
    const cgltf_accessor_sparse* Sparse = &accessor->sparse;
    cgltf_component_type IndexType = Sparse->indices_component_type;
    cgltf_size IndexSize = cgltf_component_size(IndexType);
    bool IsIndexTypeValid = (IndexType == cgltf_component_type_r_8u) || (IndexType == cgltf_component_type_r_16u)
        || (IndexType == cgltf_component_type_r_32u);
    if (!IsIndexTypeValid || (Sparse->count == 0U) || (Sparse->count > accessor->count)
        || !FitsInView(Sparse->indices_buffer_view, Sparse->indices_byte_offset, IndexSize, Sparse->count)
        || !FitsInView(Sparse->values_buffer_view, Sparse->values_byte_offset, elementSize, Sparse->count))
    {
        return Invalid(context, "accessor %" PRIu64 "'s %" PRIu64 " sparse replacements do not "
            "fit in their buffer views (or are more than its %" PRIu64 " elements).", (uint64_t)index,
            (uint64_t)Sparse->count, (uint64_t)accessor->count);
    }
    if ((((Sparse->indices_buffer_view->offset + Sparse->indices_byte_offset) % IndexSize) != 0U)
        || (((Sparse->values_buffer_view->offset + Sparse->values_byte_offset) % componentSize) != 0U))
    {
        return Invalid(context, "accessor %" PRIu64 "'s sparse replacements are not aligned to "
            "their components.", (uint64_t)index);
    }
    return Error_CreateSuccess();
}

/* The accessors: a valid type, at least one element, every element inside its view (the last one's end reckoned
 * without overflow), aligned to its components, and sparse replacements within theirs. An accessor without a view
 * reads as zeros (its count is bounded where a clip uses it: the key limit). */
static Error CheckAccessors(const GltfReadContext* context, const cgltf_data* data)
{
    const cgltf_data* Data = data;
    for (cgltf_size Index = 0U; Index < Data->accessors_count; Index++)
    {
        const cgltf_accessor* Accessor = &Data->accessors[Index];
        cgltf_size ElementSize = cgltf_calc_size(Accessor->type, Accessor->component_type);
        cgltf_size ComponentSize = cgltf_component_size(Accessor->component_type);
        if ((ElementSize == 0U) || (ComponentSize == 0U) || (Accessor->count == 0U))
        {
            return Invalid(context, "accessor %" PRIu64 " has no valid type or no elements.",
                (uint64_t)Index);
        }
        if (Accessor->is_sparse)
        {
            Error Sparse = CheckSparse(context, Index, Accessor, ElementSize, ComponentSize);
            if (Sparse.Code != ErrorCode_Success)
            {
                return Sparse;
            }
        }
        const cgltf_buffer_view* View = Accessor->buffer_view;
        if (View == NULL)
        {
            continue;
        }
        if ((Accessor->stride < ElementSize) || (ElementSize > View->size)
            || (Accessor->offset > (View->size - ElementSize))
            || ((Accessor->count - 1U) > ((View->size - ElementSize - Accessor->offset) / Accessor->stride)))
        {
            return Invalid(context, "accessor %" PRIu64 " (%" PRIu64 " elements of %" PRIu64
                " bytes) does not fit in its buffer view.", (uint64_t)Index, (uint64_t)Accessor->count,
                (uint64_t)ElementSize);
        }
        if ((((View->offset + Accessor->offset) % ComponentSize) != 0U) || ((Accessor->stride % ComponentSize) != 0U))
        {
            return Invalid(context, "accessor %" PRIu64 " is not aligned to its %" PRIu64
                "-byte components.", (uint64_t)Index, (uint64_t)ComponentSize);
        }
    }
    return Error_CreateSuccess();
}

/* Every loaded buffer's data starts aligned (a .glb's binary chunk sits where its JSON chunk's length puts it). */
static Error CheckBufferAlignment(const GltfReadContext* context, const cgltf_data* data)
{
    const cgltf_data* Data = data;
    for (cgltf_size Index = 0U; Index < Data->buffers_count; Index++)
    {
        if ((Data->buffers[Index].data != NULL) && (((uintptr_t)Data->buffers[Index].data % BUFFER_DATA_ALIGNMENT) != 0U))
        {
            return Invalid(context, "buffer %" PRIu64 "'s data is not aligned to four bytes (a "
                ".glb's chunks must be padded to four).", (uint64_t)Index);
        }
    }
    return Error_CreateSuccess();
}

// Public functions (GltfChecks.h).
Error GltfChecks_Fail(const char* description, ErrorCode code, const char* format, ...)
{
    char Text[MESSAGE_CAPACITY];
    va_list Arguments;
    va_start(Arguments, format);
    int Length = vsnprintf(Text, sizeof(Text), format, Arguments);
    va_end(Arguments);
    if (Length < 0)
    {
        Text[0] = '\0';
    }
    return Error_Construct4(code, "%s: %s", description, Text);
}

const char* GltfChecks_Printable(const char* text, char* buffer, size_t capacity)
{
    size_t Length = 0U;
    for (const char* Cursor = text; (Cursor != NULL) && (*Cursor != '\0') && ((Length + 1U) < capacity); Cursor++)
    {
        unsigned char Character = (unsigned char)*Cursor;
        buffer[Length] = ((Character >= 0x20U) && (Character < 0x7FU)) ? (char)Character : '?';
        Length++;
    }
    buffer[Length] = '\0';
    return buffer;
}

Error GltfChecks_ReadLimitedFile(const unsigned char* path, size_t limit, GenericBuffer* destination)
{
    FileSystemEntryInfo Info;
    Memory_Zero(&Info, sizeof(Info));
    Error Result = FileSystem_GetEntryInfo(path, &Info);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsFile = Info._entryType == FileSystemEntryType_File;
    size_t Size = Info._sizeInBytes;
    FileSystemEntryInfo_Deconstruct(&Info);
    if (!IsFile)
    {
        return Error_Construct3(ErrorCode_IO, u8"\"%s\" is not a file.", path);
    }
    if (Size > limit)
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"\"%s\" has %" PRIu64 u8" bytes; at most %" PRIu64
            u8" are read.", path, (uint64_t)Size, (uint64_t)limit);
    }
    Result = FileSystem_ReadAllBytes(path, destination);
    if ((Result.Code == ErrorCode_Success) && (destination->_count > limit))
    {
        return Error_Construct3(ErrorCode_BufferTooLarge, u8"\"%s\" grew past %" PRIu64 u8" bytes while it was read.",
            path, (uint64_t)limit);
    }
    return Result;
}

Error GltfChecks_Parse(const GltfReadContext* context, const void* data, size_t byteCount, cgltf_data** outData)
{
    *outData = NULL;
    CgltfBudget Budget = { .Used = 0U, .Limit = AddOrSaturate(PARSE_BUDGET_BYTES, byteCount), .IsExceeded = false };
    cgltf_options Options;
    Memory_Zero(&Options, sizeof(Options));
    Options.memory.alloc_func = AllocateForCgltf;
    Options.memory.free_func = FreeForCgltf;
    Options.memory.user_data = &Budget;
    Options.file.read = ReadBufferFileForCgltf;
    Options.file.release = ReleaseBufferFileForCgltf;
    // The callback reads the limit through it; cgltf only hands it back.
    Options.file.user_data = (void*)&context->MaxBytes;

    cgltf_data* Data = NULL;
    cgltf_result Result = cgltf_parse(&Options, data, byteCount, &Data);
    if ((Result != cgltf_result_success) && Budget.IsExceeded)
    {
        return Invalid(context, "reading its glTF structure would take more than %" PRIu64 " MiB (it lists too many "
            "objects for its size).", (uint64_t)(Budget.Limit / MEBIBYTE));
    }
    if (Result != cgltf_result_success)
    {
        return Invalid(context, "the file cannot be read as glTF 2.0: %s.", DescribeCgltfResult(Result));
    }
    // The data keeps a copy of the memory options for cgltf_free, which only frees: the budget does not outlive this
    // call.
    Data->memory.user_data = NULL;
    *outData = Data;
    for (cgltf_size Index = 0U; Index < Data->buffers_count; Index++)
    {
        const char* Uri = Data->buffers[Index].uri;
        if ((Uri != NULL) && (strncmp(Uri, "data:", 5U) != 0) && !IsBufferUriSafe(Uri))
        {
            char Name[NAME_PRINT_CAPACITY];
            return Invalid(context, "buffer %" PRIu64 " names the file \"%s\", outside the library's directory.",
                (uint64_t)Index, GltfChecks_Printable(Uri, Name, sizeof(Name)));
        }
    }
    // cgltf neither loads nor validates a number this has not checked.
    Error Checked = CheckBuffers(context, Data);
    if (Checked.Code == ErrorCode_Success)
    {
        Checked = CheckBufferViews(context, Data);
    }
    if (Checked.Code == ErrorCode_Success)
    {
        Checked = CheckAccessors(context, Data);
    }
    if (Checked.Code != ErrorCode_Success)
    {
        return Checked;
    }
    Budget.Limit = AddOrSaturate(Budget.Used, context->MaxBytes);
    Result = cgltf_load_buffers(&Options, Data, (const char*)context->FilePath);
    if (Result != cgltf_result_success)
    {
        ErrorCode Code = ErrorCode_InvalidAssetData;
        if (Result == cgltf_result_io_error)
        {
            Code = ErrorCode_IO;
        }
        else if (Result == cgltf_result_file_not_found)
        {
            Code = ErrorCode_FileNotFound;
        }
        return GltfChecks_Fail(context->Description, Code, "the file's buffers cannot be loaded: %s.",
            DescribeCgltfResult(Result));
    }
    Checked = CheckBufferAlignment(context, Data);
    if (Checked.Code != ErrorCode_Success)
    {
        return Checked;
    }
    Result = cgltf_validate(Data);
    if (Result != cgltf_result_success)
    {
        return Invalid(context, "the file is not valid glTF: %s.", DescribeCgltfResult(Result));
    }
    return Error_CreateSuccess();
}
