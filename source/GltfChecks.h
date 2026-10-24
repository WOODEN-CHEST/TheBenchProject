#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "raylib/cgltf.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): reading a glTF file that may be hostile, for every reader over cgltf (the animation
 * libraries' source/AnimationLibraryGltf.c, the terrains' source/TerrainMeshGltf.c).
 *
 * cgltf's validation does its sums without overflow checks and its loader allocates every buffer's declared length, so
 * GltfChecks_Parse checks the file's structure first, in overflow-safe sums: buffer lengths against the reader's limit
 * and their data (a data URI no longer than its base64 holds), views within their buffers with glTF's strides (4 to 252
 * bytes, in fours), accessors and their sparse replacements within their views and aligned to their components, and
 * buffer files' paths after their percent escapes are decoded (relative, no "..", no drive or backslash). Only then does
 * cgltf load the buffers (a buffer file must hold its declared length, read through WRFileSystem: UTF-8 paths on every
 * platform) and validate (an acyclic node tree, sampler output counts), after which every loaded buffer's data must be
 * aligned. A reader still checks what it reads (accessor types, counts, values) before it trusts a number. cgltf's memory
 * goes through WRMemory, so a leak shows in the tests' allocation counts.
 */


// Types.
/* What one read is: what messages call the file, the most bytes the file and its buffers may hold together, and the
 * file's path for buffers kept in files beside it (NULL: none; such a buffer then cannot load). */
typedef struct GltfReadContextStruct
{
    const char* Description;
    size_t MaxBytes;
    const unsigned char* FilePath;
} GltfReadContext;


// Functions.
/* An error of @p code: "<description>: <formatted text>". */
Error GltfChecks_Fail(const char* description, ErrorCode code, const char* format, ...);

/* A name from the file in printable form (printable ASCII; anything else as '?'), for a message; NULL reads as empty. */
const char* GltfChecks_Printable(const char* text, char* buffer, size_t capacity);

/* Reads a whole file of at most @p limit bytes into @p destination (cleared first by the caller); ErrorCode_BufferTooLarge
 * past the limit, ErrorCode_IO for an entry that is not a file. */
Error GltfChecks_ReadLimitedFile(const unsigned char* path, size_t limit, GenericBuffer* destination);

/* Parses @p data (cgltf_parse), checks its structure, loads its buffers and validates it (the file comment), with every
 * allocation cgltf makes within a budget (source/GltfChecks.c: 64 MiB beyond the file's size for the parse, the limit
 * for the buffers). *outData receives the parsed data whenever the parse succeeded, also when a later check fails: the
 * caller frees it with cgltf_free. Errors are ErrorCode_InvalidAssetData naming what broke a rule (the budget too),
 * ErrorCode_FileNotFound / ErrorCode_IO for a buffer file that is missing or unreadable. */
Error GltfChecks_Parse(const GltfReadContext* context, const void* data, size_t byteCount, cgltf_data** outData);
