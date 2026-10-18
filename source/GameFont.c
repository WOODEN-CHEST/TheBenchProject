#include "GameFont.h"


// Macros.
/** The glyph drawn for codepoints a font lacks (raylib draws '?' too). */
#define GAME_FONT_FALLBACK_CODEPOINT ((CodePoint)'?')


// Static functions.
static int32_t FindExactGlyph(const GameFont* self, CodePoint codepoint)
{
    const GlyphInfo* Glyphs = self->_rayFont.glyphs;
    int32_t Count = (int32_t)self->_rayFont.glyphCount;
    if (!self->_areGlyphsSorted)
    {
        for (int32_t Index = 0; Index < Count; Index++)
        {
            if (Glyphs[Index].value == codepoint)
            {
                return Index;
            }
        }
        return -1;
    }

    int32_t Low = 0;
    int32_t High = Count - 1;
    while (Low <= High)
    {
        int32_t Middle = Low + ((High - Low) / 2);
        if (Glyphs[Middle].value == codepoint)
        {
            return Middle;
        }
        if (Glyphs[Middle].value < codepoint)
        {
            Low = Middle + 1;
        }
        else
        {
            High = Middle - 1;
        }
    }
    return -1;
}


// Public functions.
GameFont GameFont_Wrap(Font font)
{
    GameFont Wrapped =
    {
        ._rayFont = font,
        ._areGlyphsSorted = false,
        ._fallbackGlyphIndex = -1,
        ._shadowOffset = (Vector2){ .x = GAME_FONT_DEFAULT_SHADOW_OFFSET_X, .y = GAME_FONT_DEFAULT_SHADOW_OFFSET_Y }
    };
    if (!GameFont_HasGlyphs(&Wrapped))
    {
        return Wrapped;
    }

    bool IsSorted = true;
    for (int Index = 1; Index < font.glyphCount; Index++)
    {
        if (font.glyphs[Index - 1].value >= font.glyphs[Index].value)
        {
            IsSorted = false;
            break;
        }
    }
    Wrapped._areGlyphsSorted = IsSorted;
    Wrapped._fallbackGlyphIndex = FindExactGlyph(&Wrapped, GAME_FONT_FALLBACK_CODEPOINT);
    return Wrapped;
}

bool GameFont_HasGlyph(const GameFont* self, CodePoint codepoint)
{
    return GameFont_HasGlyphs(self) && (FindExactGlyph(self, codepoint) >= 0);
}

int32_t GameFont_FindGlyphIndex(const GameFont* self, CodePoint codepoint)
{
    if (!GameFont_HasGlyphs(self))
    {
        return -1;
    }
    int32_t Index = FindExactGlyph(self, codepoint);
    return (Index >= 0) ? Index : self->_fallbackGlyphIndex;
}

float GameFont_GetGlyphAdvance(const GameFont* self, int32_t glyphIndex)
{
    const GlyphInfo* Glyph = &self->_rayFont.glyphs[glyphIndex];
    if (Glyph->advanceX > 0)
    {
        return (float)Glyph->advanceX;
    }
    return self->_rayFont.recs[glyphIndex].width + (float)Glyph->offsetX;
}
