#pragma once
#include <stddef.h>
#include <stdint.h>
#include "wr/WRJSON.h"
#include "wr/WRError.h"


/*
 * The "codepoints" key of font definitions (private to the font loader; tests use it too).
 *
 * An entry is a block name (basic_latin, latin_1_supplement, latin_extended_a, latin_extended_b, cyrillic: blocks
 * whose every codepoint is assigned), one codepoint, or a [first, last] range; numbers may also be strings such as
 * "0x400". Without the key a font gets basic_latin, latin_1_supplement and latin_extended_a. '?' and ' ' are always
 * added: '?' is drawn for every codepoint a font lacks, and the space's advance spaces words.
 */


// Macros.
/* Most codepoints one font may rasterize, after merging every entry. */
#define FONT_CODEPOINTS_MAX ((int64_t)8192)
/* Most entries the "codepoints" array may have (ranges cover more than this when needed). */
#define FONT_CODEPOINTS_MAX_ENTRY_COUNT ((size_t)1024)
/* Glyph count times the pixel size squared may not exceed one 8192 x 8192 atlas (the sprite sheet limit too). */
#define FONT_CODEPOINTS_MAX_ATLAS_AREA ((uint64_t)8192U * (uint64_t)8192U)


// Functions.
/*
 * Reads root's "codepoints" into an owned array of ascending, unique codepoints (int, as LoadFontEx takes them),
 * checked against FONT_CODEPOINTS_MAX_ENTRY_COUNT, FONT_CODEPOINTS_MAX and, for pixelSize, against the atlas limit.
 * Release *outCodepoints with
 * Memory_Free. Bad entries fail with ErrorCode_InvalidAssetDefinition naming the entry.
 */
Error FontCodepoints_Read(JSONCompound* root, int pixelSize, int** outCodepoints, int* outCount);
