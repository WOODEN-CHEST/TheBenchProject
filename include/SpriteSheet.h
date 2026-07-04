#pragma once
#include "wr/WRMemory.h"
#include "wr/WRError.h"
#include "raylib/raylib.h"


/**
 * A sprite sheet just stores a bunch of sprites in a single image for performance reasons.
 *
 * The sprite sheet object does not own the buffer or texture passed into it and thus simply
 * deconstructing the sprite sheet does not free the memory it uses. That has to be done
 * by the same part of the code which constructed and passed in the texture and buffer when constructing the sprite sheet.
 */


/**
 * @brief Size in bytes of a sprite sheet entry's name buffer, including the null terminator.
 *
 * An entry name is a null-terminated UTF-8 string that must fit within this many bytes (terminator
 * included), so the longest storable name is SPRITE_SHEET_ENTRY_MAX_NAME_LENGTH - 1 bytes.
 */
#define SPRITE_SHEET_ENTRY_MAX_NAME_LENGTH 128

/**
 * @brief A single named region of a sprite sheet's texture.
 *
 * Maps a name to the rectangular area within the sheet texture that holds that sprite. Entries are
 * stored by value inside the GenericBuffer handed to SpriteSheet_Construct1; the sprite sheet looks
 * them up by name via SpriteSheet_GetTextureArea. The fields are populated by whoever builds the
 * entry buffer (for example the asset loader) before the buffer is given to the sprite sheet.
 */
typedef struct SpriteSheetEntryStruct
{
    /** @brief The entry's name as a null-terminated UTF-8 string; at most SPRITE_SHEET_ENTRY_MAX_NAME_LENGTH bytes including the terminator. */
    unsigned char _name[SPRITE_SHEET_ENTRY_MAX_NAME_LENGTH];
    /** @brief The sprite's region within the sheet texture, in texture pixel coordinates. */
    Rectangle _textureArea;
} SpriteSheetEntry;

/**
 * @brief A texture partitioned into named sprite regions.
 *
 * Wraps a single texture and a buffer of SpriteSheetEntry records that name sub-regions of it.
 * Neither the texture nor the entry buffer is owned: they are borrowed for the lifetime of the sprite
 * sheet and must outlive it, and SpriteSheet_Deconstruct frees neither. Create with
 * SpriteSheet_Construct1 and release with SpriteSheet_Deconstruct.
 */
typedef struct SpriteSheetStruct
{
    /** @brief The backing sheet texture; borrowed, not owned. */
    Texture2D _texture;
    /** @brief Borrowed buffer of SpriteSheetEntry records describing the named regions; not owned. */
    GenericBuffer* _entries;
} SpriteSheet;


// Functions.
/**
 * @brief Initializes a sprite sheet over a borrowed texture and entry buffer.
 *
 * Stores @p texture and @p entries by reference; neither is copied or owned, so both must remain
 * valid for the lifetime of the sprite sheet and are not released by SpriteSheet_Deconstruct.
 * @param self The sprite sheet to initialize; must not be NULL.
 * @param texture The backing sheet texture; borrowed and must outlive @p self.
 * @param entries Buffer of SpriteSheetEntry records naming regions of @p texture; must not be NULL,
 *        is borrowed, and must outlive @p self.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p entries is NULL.
 */
Error SpriteSheet_Construct1(SpriteSheet* self, Texture2D texture, GenericBuffer* entries);

/**
 * @brief Releases a sprite sheet, leaving its borrowed texture and entry buffer untouched.
 *
 * Clears the sprite sheet's fields. The borrowed texture and entry buffer are not freed; releasing
 * them is the responsibility of whoever created and passed them in.
 * @param self The sprite sheet to deconstruct; must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error SpriteSheet_Deconstruct(SpriteSheet* self);

/**
 * @brief Looks up the texture region of the entry with the given name.
 *
 * Performs an exact-match linear search over the entries and, on a match, writes that entry's region
 * (in texture pixel coordinates) to @p outArea. @p outArea is zeroed before the search begins.
 * @param self The sprite sheet to query; must not be NULL.
 * @param entryName The exact, null-terminated UTF-8 name to look up; must not be NULL.
 * @param outArea [out] Receives the matched entry's texture region; must not be NULL. Zeroed on entry
 *        and left zeroed if no match is found.
 * @returns ErrorCode_Success if a matching entry was found; ErrorCode_IllegalArgument if @p self is
 *          NULL; ErrorCode_InvalidOperation if no entry has the given name; ErrorCode_InvalidState if
 *          an entry slot is unexpectedly missing (internal inconsistency).
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error SpriteSheet_GetTextureArea(SpriteSheet* self, const unsigned char* entryName, Rectangle* outArea);
