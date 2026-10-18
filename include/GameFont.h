#pragma once
#include <stdbool.h>
#include <stdint.h>
// raylib's Font is embedded by value, so the wrapper needs the full raylib type.
#include "raylib/raylib.h"
#include "wr/WRUnicode.h"


/**
 * @brief A loaded font: a raylib Font (glyph atlas texture, glyph metrics and rectangles) plus lookup data.
 *
 * Obtained from AssetManager_LoadFont and owned by the asset manager: the pointer is borrowed and stays valid only
 * while the asset user that loaded it holds the font. Releasing the last hold unloads the font (UnloadFont), which
 * frees the glyph arrays and the atlas texture. A font is rasterized once at load time, at the pixel size and for
 * the codepoints its definition names (references/asset_structure.md, "Fonts"); a codepoint outside them draws as
 * '?'.
 *
 * Do not copy the wrapper or its Font by value into anything that can outlive that asset user: the copy shares the
 * glyph pointers and the texture id, which dangle once the font is unloaded. Text refers to fonts by asset name and
 * lets its TextDocument hold them.
 */
typedef struct GameFontStruct
{
    /** @brief The raylib font; owned by the asset manager, read-only for consumers. */
    Font _rayFont;
    /** @brief true when the glyphs are ordered by ascending codepoint, so lookups can binary-search. */
    bool _areGlyphsSorted;
    /** @brief Index of the '?' glyph drawn for codepoints the font lacks, or -1 when the font has none. */
    int32_t _fallbackGlyphIndex;
} GameFont;


/**
 * @brief Wraps a raylib font and computes its lookup data (glyph order, fallback glyph).
 *
 * The font loader uses it for every font asset; TextDocument uses it for raylib's built-in font, which is not an
 * asset (raylib owns it until CloseWindow). The wrapper does not own @p font.
 * @param font The raylib font; may have no glyphs (raylib's built-in font before InitWindow), then every lookup fails.
 * @returns The wrapper.
 */
GameFont GameFont_Wrap(Font font);

/**
 * @brief Reports whether the font has glyph data to measure and draw with.
 * @param self The font; must not be NULL.
 * @returns false for an empty font (for example raylib's built-in font before InitWindow).
 */
static inline bool GameFont_HasGlyphs(const GameFont* self)
{
    return (self->_rayFont.glyphCount > 0) && (self->_rayFont.glyphs != NULL) && (self->_rayFont.recs != NULL)
        && (self->_rayFont.baseSize > 0);
}

/**
 * @brief Reports whether the font has its own glyph for a codepoint (the '?' fallback does not count).
 * @param self The font; must not be NULL.
 * @param codepoint The codepoint to look up.
 * @returns true if the font was rasterized with a glyph for @p codepoint.
 */
bool GameFont_HasGlyph(const GameFont* self, CodePoint codepoint);

/**
 * @brief Finds the glyph that draws a codepoint: its own glyph, else the '?' fallback.
 *
 * O(log n) for fonts with sorted glyphs (every font asset; raylib's built-in font), O(n) otherwise.
 * @param self The font; must not be NULL.
 * @param codepoint The codepoint to draw.
 * @returns The glyph index, or -1 when the font has neither the codepoint nor a '?' glyph.
 */
int32_t GameFont_FindGlyphIndex(const GameFont* self, CodePoint codepoint);

/**
 * @brief Returns how far a glyph advances the pen, in font pixels at the font's base size (as raylib measures it).
 * @param self The font; must not be NULL.
 * @param glyphIndex A glyph index from GameFont_FindGlyphIndex; must be valid.
 * @returns The advance, in pixels at @c _rayFont.baseSize.
 */
float GameFont_GetGlyphAdvance(const GameFont* self, int32_t glyphIndex);
