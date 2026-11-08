#include <math.h>
#include <stdint.h>
#include "TextLayout.h"
#include "wr/WRChar.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/* An atom index meaning "none". */
#define TEXT_LAYOUT_NO_ATOM SIZE_MAX
/* The line of an atom dropped by wrapping (the spaces at a soft break). */
#define TEXT_LAYOUT_LINE_DROPPED SIZE_MAX
/* Factor the component brightness is multiplied by for the default shadow color. */
#define TEXT_LAYOUT_SHADOW_BRIGHTNESS_FACTOR (0.25f)
/* Underline and strikethrough thickness as a fraction of the font size. */
#define TEXT_LAYOUT_DECORATION_THICKNESS (0.06f)
/* Height of the strikethrough within a run, as a fraction of the run height. */
#define TEXT_LAYOUT_STRIKETHROUGH_HEIGHT (0.5f)
/* Largest RGB channel value. */
#define TEXT_LAYOUT_COLOR_CHANNEL_MAX (255)
/* First codepoint that is drawn (lower ones are control characters, except '\n' and '\t'), and the other control
 * characters: DEL and the C1 range. */
#define TEXT_LAYOUT_FIRST_PRINTABLE ((CodePoint)0x20)
#define TEXT_LAYOUT_DELETE ((CodePoint)0x7F)
#define TEXT_LAYOUT_C1_FIRST ((CodePoint)0x80)
#define TEXT_LAYOUT_C1_LAST ((CodePoint)0x9F)
/* What a sprite is in the flow's text: U+FFFC OBJECT REPLACEMENT CHARACTER, three bytes of UTF-8. */
#define TEXT_LAYOUT_OBJECT_CHARACTER_BYTES ((size_t)3)
/* The longest flow a build that tracks offsets keeps: its offsets are 32-bit. */
#define TEXT_LAYOUT_MAX_TRACKED_BYTES ((size_t)UINT32_MAX)
/* How far, in a run's font sizes, a glyph's drawing may reach past its pen position and its run's box (a wide glyph, a negative bearing,
 * an underline below the box): culling keeps everything within it. */
#define TEXT_LAYOUT_CULL_REACH_FONT_SIZES (2.0f)


// Types.
typedef enum TextLayoutRunTypeEnum
{
    TextLayoutRunType_Glyphs,
    TextLayoutRunType_Sprite
} TextLayoutRunType;

/* What a tracked character is to a caret: shown on its line, a line's '\n', or a space a wrap dropped (both stand at their line's
 * end, with no width). */
typedef enum TextLayoutCharacterKindEnum
{
    TextLayoutCharacterKind_Shown,
    TextLayoutCharacterKind_Break,
    TextLayoutCharacterKind_Dropped
} TextLayoutCharacterKind;

/* A character of the flow a tracked build keeps: where its bytes start in the flow's text and how many, its line, and its place
 * (block-local fitted units, final: after shrinking and alignment). */
typedef struct TextLayoutCharacterStruct
{
    uint32_t ByteOffset;
    uint32_t Line;
    float X;
    float Advance;
    uint8_t ByteCount;
    uint8_t Kind;
} TextLayoutCharacter;

/* Where a resolved glyph's bytes are in the flow's text (a tracked build's scratch, beside its ScratchGlyph). */
typedef struct ScratchOffsetStruct
{
    uint32_t ByteOffset;
    uint8_t ByteCount;
} ScratchOffset;

/* One laid-out piece: a span of glyphs sharing a style on one line, or one sprite box. Final block-local fitted
 * units. */
typedef struct TextLayoutRunStruct
{
    TextLayoutRunType Type;
    float X;
    float Y;
    float Width;
    float Height;
    /* Build-time merging: the style and line the run belongs to. */
    size_t StyleIndex;
    size_t Line;
    /* Glyph runs. */
    const GameFont* Font;
    float FontSize;
    RenderColor Color;
    bool IsShadowActive;
    RenderColor ShadowColor;
    Vector2 ShadowOffset;
    bool IsUnderlined;
    bool IsStrikethrough;
    size_t FirstGlyph;
    size_t GlyphCount;
    /* How far underline and strikethrough reach from X: to the end of the run's last glyph, and never past the end of
     * the line's last glyph or sprite (trailing spaces and the spacing after the last glyph stay undecorated). */
    float DecorationWidth;
    /* Sprite runs. */
    SpriteAnimationInstance* Instance;
    RenderColor SpriteColor;
} TextLayoutRun;

/* A drawable glyph of a glyph run: its glyph index and pen X (block-local fitted); the pen Y is the run's Y. */
typedef struct TextLayoutGlyphStruct
{
    int32_t GlyphIndex;
    float X;
} TextLayoutGlyph;

/* The resolved style of one text-bearing component. */
typedef struct LayoutStyleStruct
{
    const GameFont* Font;
    /* Line height, fitted (component size times the multiplier). */
    float FontSize;
    /* Fitted units per font pixel; 0 for a font without glyphs. */
    float Scale;
    /* Fitted space added after each glyph. */
    float Spacing;
    RenderColor Color;
    bool IsShadowActive;
    RenderColor ShadowColor;
    /* Fraction of the size. */
    Vector2 ShadowOffset;
    bool IsUnderlined;
    bool IsStrikethrough;
} LayoutStyle;

typedef enum LayoutAtomKindEnum
{
    LayoutAtomKind_Word,
    LayoutAtomKind_Space,
    LayoutAtomKind_Sprite,
    LayoutAtomKind_Break
} LayoutAtomKind;

/* An unbreakable piece of the flow: a word (consecutive glyphs of one style), a run of spaces and tabs (a break
 * opportunity), a sprite, or a line break. Sizes are fitted, before shrinking. */
typedef struct LayoutAtomStruct
{
    LayoutAtomKind Kind;
    size_t StyleIndex;
    size_t FirstGlyph;
    size_t GlyphCount;
    float Width;
    float Height;
    /* Word and space atoms: what the last glyph's advance adds to its width (its spacing); a line that ends on this
     * atom does not count it. */
    float TrailingSpacing;
    SpriteAnimationInstance* Instance;
    RenderColor SpriteColor;
    size_t Line;
    /* Break and sprite atoms of a tracked build: where the '\n' or the sprite's object character is in the flow's text. */
    uint32_t ByteOffset;
} LayoutAtom;

/* A resolved glyph of the flow: its glyph index (-1 for whitespace or a glyph the font cannot draw) and its fitted
 * advance including the spacing. */
typedef struct ScratchGlyphStruct
{
    int32_t GlyphIndex;
    float Advance;
} ScratchGlyph;

/* One laid-out line. Width (up to the end of its last glyph or sprite: trailing whitespace does not count) and Height are
 * before shrinking; Scale is the shrink factor; Top and Left are final. */
typedef struct LayoutLineStruct
{
    float Width;
    float Height;
    float Scale;
    float Top;
    float Left;
    /* A tracked build's characters of the line: [FirstCharacter, FirstCharacter + CharacterCount). */
    size_t FirstCharacter;
    size_t CharacterCount;
} LayoutLine;

/* What a build walks with. */
typedef struct BuildContextStruct
{
    TextLayout* Layout;
    TextDocument* Document;
    const TextLayoutOptions* Options;
    /* Whether the build keeps the flow's text and its characters (TextLayoutOptions.TracksOffsets). */
    bool IsTracking;
} BuildContext;


// Static functions: helpers.
static bool IsFloatInRange(float value, float minimum, float maximum)
{
    return isfinite((double)value) && (value >= minimum) && (value <= maximum);
}

static Error GrowthError(void)
{
    return Error_Construct2(ErrorCode_BufferTooLarge, "TextLayout: could not grow a layout buffer.");
}

static LayoutAtom* GetAtom(TextLayout* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_atoms, index);
}

static LayoutAtom* GetLastAtom(TextLayout* self)
{
    return (self->_atoms._count > 0U) ? GetAtom(self, self->_atoms._count - 1U) : NULL;
}

static LayoutStyle* GetStyle(TextLayout* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_styles, index);
}

static LayoutLine* GetLine(TextLayout* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_lines, index);
}

// The draw path only has a const layout; the buffers hold the element types named here.
static const TextLayoutRun* GetRuns(const TextLayout* self)
{
    return (const TextLayoutRun*)self->_runs._data;
}

static const TextLayoutGlyph* GetGlyphs(const TextLayout* self)
{
    return (const TextLayoutGlyph*)self->_glyphs._data;
}

static void ClearBuild(TextLayout* self)
{
    GenericBuffer_Clear(&self->_runs);
    GenericBuffer_Clear(&self->_glyphs);
    GenericBuffer_Clear(&self->_lines);
    GenericBuffer_Clear(&self->_styles);
    GenericBuffer_Clear(&self->_atoms);
    GenericBuffer_Clear(&self->_scratchGlyphs);
    GenericBuffer_Clear(&self->_scratchOffsets);
    GenericBuffer_Clear(&self->_flowText);
    GenericBuffer_Clear(&self->_characters);
    self->_emptyLineHeight = 0.0f;
    self->_caretExtent = 0.0f;
    self->_size = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_isBuilt = false;
    self->_isWaitingForGlyphs = false;
    self->_document = NULL;
}

static Error ValidateOptions(const TextLayoutOptions* options)
{
    if (!IsFloatInRange(options->SizeMultiplier, 0.0f, TEXT_LAYOUT_SIZE_MULTIPLIER_MAX))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "TextLayout: the size multiplier must be finite and within [0, TEXT_LAYOUT_SIZE_MULTIPLIER_MAX].");
    }
    if (options->HasMaxWidth && (!isfinite((double)options->MaxWidth) || (options->MaxWidth < 0.0f)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "TextLayout: the maximum width must be finite and >= 0.");
    }
    switch (options->Alignment)
    {
        case TextLayoutAlignment_Left:
        case TextLayoutAlignment_Center:
        case TextLayoutAlignment_Right:
            return Error_CreateSuccess();
    }
    return Error_Construct2(ErrorCode_IllegalArgument, "TextLayout: unknown alignment.");
}

// The maximum width and what uses it only matter with HasMaxWidth.
static bool AreOptionsEqual(const TextLayoutOptions* a, const TextLayoutOptions* b)
{
    if ((a->SizeMultiplier != b->SizeMultiplier) || (a->HasMaxWidth != b->HasMaxWidth) || (a->Alignment != b->Alignment)
        || (a->TracksOffsets != b->TracksOffsets))
    {
        return false;
    }
    return !a->HasMaxWidth || ((a->MaxWidth == b->MaxWidth) && (a->IsWrapEnabled == b->IsWrapEnabled)
        && (a->IsShrinkEnabled == b->IsShrinkEnabled));
}

static RenderColor MultiplyRenderColor(RenderColor a, RenderColor b)
{
    RenderColor Result;
    Result.Tint.r = (unsigned char)(((int)a.Tint.r * (int)b.Tint.r) / TEXT_LAYOUT_COLOR_CHANNEL_MAX);
    Result.Tint.g = (unsigned char)(((int)a.Tint.g * (int)b.Tint.g) / TEXT_LAYOUT_COLOR_CHANNEL_MAX);
    Result.Tint.b = (unsigned char)(((int)a.Tint.b * (int)b.Tint.b) / TEXT_LAYOUT_COLOR_CHANNEL_MAX);
    Result.Tint.a = (unsigned char)TEXT_LAYOUT_COLOR_CHANNEL_MAX; // RenderColor ignores the tint alpha; opacity carries it.
    Result.Brightness = a.Brightness * b.Brightness;
    Result.Opacity = a.Opacity * b.Opacity;
    return Result;
}


// Static functions: flattening the tree into atoms.
static Error AddStyle(BuildContext* context, const StringComponent* string, size_t* outStyleIndex)
{
    const GameFont* ResolvedFont = NULL;
    Error Result = TextDocument_ResolveFont(context->Document, StringComponent_GetFontName(string), &ResolvedFont);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // raylib's built-in font has no glyphs before InitWindow: such a build is redone once it has them.
    if (!GameFont_HasGlyphs(ResolvedFont))
    {
        context->Layout->_isWaitingForGlyphs = true;
    }

    float FontSize = StringComponent_GetSize(string) * context->Options->SizeMultiplier;
    LayoutStyle Style =
    {
        .Font = ResolvedFont,
        .FontSize = FontSize,
        .Scale = GameFont_HasGlyphs(ResolvedFont) ? (FontSize / (float)ResolvedFont->_rayFont.baseSize) : 0.0f,
        .Spacing = StringComponent_GetSpacing(string) * FontSize,
        .Color = StringComponent_GetColor(string),
        .IsShadowActive = StringComponent_IsShadowActive(string),
        .ShadowOffset = StringComponent_ResolveShadowOffset(string, GameFont_GetShadowOffset(ResolvedFont)),
        .IsUnderlined = StringComponent_IsUnderlined(string),
        .IsStrikethrough = StringComponent_IsStrikethrough(string)
    };
    if (StringComponent_GetShadowColorType(string) == TextShadowColorType_Custom)
    {
        Style.ShadowColor = StringComponent_GetShadowColor(string);
    }
    else
    {
        Style.ShadowColor = Style.Color;
        Style.ShadowColor.Brightness = Style.Color.Brightness * TEXT_LAYOUT_SHADOW_BRIGHTNESS_FACTOR;
    }

    if (!GenericBuffer_AddLast(&context->Layout->_styles, &Style))
    {
        return GrowthError();
    }
    *outStyleIndex = context->Layout->_styles._count - 1U;
    if (*outStyleIndex == 0U)
    {
        context->Layout->_emptyLineHeight = FontSize; // An empty text's caret is as tall as its first text's line.
    }
    return Error_CreateSuccess();
}

// byteOffset is where a break's '\n' or a sprite's object character is in the flow's text (a tracked build's; 0 otherwise).
static Error AddAtom(TextLayout* self, LayoutAtomKind kind, size_t styleIndex, float height, uint32_t byteOffset)
{
    LayoutAtom Atom =
    {
        .Kind = kind,
        .StyleIndex = styleIndex,
        .FirstGlyph = self->_scratchGlyphs._count,
        .GlyphCount = 0U,
        .Width = 0.0f,
        .Height = height,
        .TrailingSpacing = 0.0f,
        .Instance = NULL,
        .SpriteColor = RenderColor_White(),
        .Line = 0U,
        .ByteOffset = byteOffset
    };
    return GenericBuffer_AddLast(&self->_atoms, &Atom) ? Error_CreateSuccess() : GrowthError();
}

// Appends a glyph to the word or space run of this style that the flow is in, starting a new atom when needed.
// advance includes the spacing; rawAdvance is the glyph alone (what it adds when it ends a line). A tracked build also keeps where the
// glyph's bytes are in the flow's text (place).
static Error AppendGlyph(BuildContext* context, LayoutAtomKind kind, size_t styleIndex, int32_t glyphIndex, float advance,
    float rawAdvance, ScratchOffset place)
{
    TextLayout* Self = context->Layout;
    LayoutAtom* Last = GetLastAtom(Self);
    if ((Last == NULL) || (Last->Kind != kind) || (Last->StyleIndex != styleIndex))
    {
        Error Result = AddAtom(Self, kind, styleIndex, GetStyle(Self, styleIndex)->FontSize, 0U);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Last = GetLastAtom(Self);
    }

    ScratchGlyph Glyph = { .GlyphIndex = glyphIndex, .Advance = advance };
    if (!GenericBuffer_AddLast(&Self->_scratchGlyphs, &Glyph))
    {
        return GrowthError();
    }
    if (context->IsTracking && !GenericBuffer_AddLast(&Self->_scratchOffsets, &place))
    {
        return GrowthError();
    }
    Last->GlyphCount++;
    Last->Width += advance;
    Last->TrailingSpacing = advance - rawAdvance;
    return Error_CreateSuccess();
}

// A tracked build adds text to the flow's text, and gives back where it starts. Its offsets are 32-bit.
static Error AppendFlowText(TextLayout* self, const unsigned char* text, size_t length, uint32_t* outStart)
{
    if (self->_flowText._count > (TEXT_LAYOUT_MAX_TRACKED_BYTES - length))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "TextLayout: a flow that tracks offsets must stay below 4 GiB.");
    }
    *outStart = (uint32_t)self->_flowText._count;
    // AppendRangeBytes only reads its source; the API is just not declared const.
    if ((length > 0U) && !GenericBuffer_AppendRangeBytes(&self->_flowText, (unsigned char*)text, length))
    {
        return GrowthError();
    }
    return Error_CreateSuccess();
}

// The fitted advance of a glyph of the style's font without the spacing; 0 when the font cannot draw it.
static float GetRawAdvance(const LayoutStyle* style, int32_t glyphIndex)
{
    if ((glyphIndex < 0) || (style->Scale <= 0.0f))
    {
        return 0.0f;
    }
    return GameFont_GetGlyphAdvance(style->Font, glyphIndex) * style->Scale;
}

// The fitted advance with the spacing; never negative, so a strongly negative spacing stacks glyphs at most.
static float GetSpacedAdvance(const LayoutStyle* style, float rawAdvance)
{
    return (rawAdvance > 0.0f) ? Math_MaxFloat(0.0f, rawAdvance + style->Spacing) : 0.0f;
}

static bool IsControlCharacter(CodePoint codepoint)
{
    return (codepoint < TEXT_LAYOUT_FIRST_PRINTABLE) || (codepoint == TEXT_LAYOUT_DELETE)
        || ((codepoint >= TEXT_LAYOUT_C1_FIRST) && (codepoint <= TEXT_LAYOUT_C1_LAST));
}

static Error EmitText(BuildContext* context, size_t styleIndex, const unsigned char* text)
{
    TextLayout* Self = context->Layout;
    const LayoutStyle* Style = GetStyle(Self, styleIndex);
    float SpaceRaw = GetRawAdvance(Style, GameFont_FindGlyphIndex(Style->Font, (CodePoint)' '));
    float SpaceAdvance = GetSpacedAdvance(Style, SpaceRaw);
    float TabCount = (float)TEXT_LAYOUT_TAB_SPACE_COUNT;
    uint32_t FlowStart = 0U;
    if (context->IsTracking)
    {
        Error Result = AppendFlowText(Self, text, StringUTF8_GetByteLength(text), &FlowStart);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    const unsigned char* Cursor = text;
    while (*Cursor != (unsigned char)0)
    {
        size_t ByteCount = CharUTF8_GetByteCountChar(Cursor);
        CodePoint Codepoint = CharUTF8_GetCodePoint(Cursor);
        if ((ByteCount == 0U) || (Codepoint == CODEPOINT_NONE))
        {
            // Component text is validated UTF-8, so this only skips a byte of damaged text.
            Cursor++;
            continue;
        }
        ScratchOffset Place = { .ByteOffset = FlowStart + (uint32_t)(Cursor - text), .ByteCount = (uint8_t)ByteCount };
        Cursor += ByteCount;

        Error Result = Error_CreateSuccess();
        if (Codepoint == (CodePoint)'\n')
        {
            Result = AddAtom(Self, LayoutAtomKind_Break, styleIndex, Style->FontSize, Place.ByteOffset);
        }
        else if (Codepoint == (CodePoint)' ')
        {
            Result = AppendGlyph(context, LayoutAtomKind_Space, styleIndex, -1, SpaceAdvance, SpaceRaw, Place);
        }
        else if (Codepoint == (CodePoint)'\t')
        {
            Result = AppendGlyph(context, LayoutAtomKind_Space, styleIndex, -1, SpaceAdvance * TabCount, SpaceRaw * TabCount, Place);
        }
        else if (!IsControlCharacter(Codepoint))
        {
            int32_t GlyphIndex = GameFont_FindGlyphIndex(Style->Font, Codepoint);
            float Raw = GetRawAdvance(Style, GlyphIndex);
            Result = AppendGlyph(context, LayoutAtomKind_Word, styleIndex, GlyphIndex, GetSpacedAdvance(Style, Raw), Raw, Place);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

// The text a translate component shows: its translation, else its fallback, else its key.
static const unsigned char* GetTranslateText(const TextDocument* document, const TranslateComponent* translate)
{
    const unsigned char* Translation = TextDocument_ResolveTranslation(document, TranslateComponent_GetKey(translate));
    if (Translation != NULL)
    {
        return Translation;
    }
    const unsigned char* Fallback = TranslateComponent_GetFallback(translate);
    return (Fallback[0] != (unsigned char)0) ? Fallback : TranslateComponent_GetKey(translate);
}

static Error EmitSprite(BuildContext* context, const SpriteComponent* sprite)
{
    TextLayout* Layout = context->Layout;
    SpriteAnimationInstance* Instance = NULL;
    Error Result = TextDocument_ResolveAnimation(context->Document, SpriteComponent_GetAnimationName(sprite), &Instance);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    // In a tracked build's flow a sprite is one object character, so a caret can stand before it and after it.
    uint32_t FlowStart = 0U;
    if (context->IsTracking)
    {
        static const unsigned char ObjectCharacter[TEXT_LAYOUT_OBJECT_CHARACTER_BYTES] = { 0xEFU, 0xBFU, 0xBCU };
        Result = AppendFlowText(Layout, ObjectCharacter, TEXT_LAYOUT_OBJECT_CHARACTER_BYTES, &FlowStart);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Vector2 Size = SpriteComponent_GetSize(sprite);
    float Multiplier = context->Options->SizeMultiplier;
    Result = AddAtom(Layout, LayoutAtomKind_Sprite, 0U, Size.y * Multiplier, FlowStart);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    LayoutAtom* Atom = GetLastAtom(Layout);
    Atom->Width = Size.x * Multiplier;
    Atom->Instance = Instance;
    Atom->SpriteColor = SpriteComponent_GetColor(sprite);
    return Error_CreateSuccess();
}

// Own content first, then each child in order. Recursion depth is bounded by the tree's depth limit.
static Error Flatten(BuildContext* context, const TextComponent* component)
{
    Error Result = Error_CreateSuccess();
    switch (TextComponent_GetType(component))
    {
        case TextComponentType_String:
        {
            const StringComponent* String = TextComponent_AsConstStringComponent(component);
            size_t StyleIndex = 0U;
            Result = AddStyle(context, String, &StyleIndex);
            if (Result.Code == ErrorCode_Success)
            {
                Result = EmitText(context, StyleIndex, StringComponent_GetText(String));
            }
            break;
        }
        case TextComponentType_Translate:
        {
            const TranslateComponent* Translate = TextComponent_AsConstTranslateComponent(component);
            size_t StyleIndex = 0U;
            Result = AddStyle(context, &Translate->Base, &StyleIndex);
            if (Result.Code == ErrorCode_Success)
            {
                Result = EmitText(context, StyleIndex, GetTranslateText(context->Document, Translate));
            }
            break;
        }
        case TextComponentType_Sprite:
            Result = EmitSprite(context, TextComponent_AsConstSpriteComponent(component));
            break;
        case TextComponentType_Empty:
            break;
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    size_t ChildCount = TextComponent_GetSubComponentCount(component);
    for (size_t Index = 0; Index < ChildCount; Index++)
    {
        Result = Flatten(context, TextComponent_GetSubComponentAt(component, Index));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}


// Static functions: breaking lines.
// Greedy line assignment. With wrapping, a word or sprite that would end past the maximum width moves the flow after
// the last space following content on the line to a new line, and drops that space run. Returns the line count (a
// final line break adds no empty line after it).
static size_t AssignLines(TextLayout* self, const TextLayoutOptions* options)
{
    // A tracked build gives a final line break an empty line after it, where the caret after it stands.
    bool IsWrapping = options->HasMaxWidth && options->IsWrapEnabled;
    size_t AtomCount = self->_atoms._count;
    size_t Line = 0U;
    float LineWidth = 0.0f;
    bool HasContent = false;
    size_t BreakSpace = TEXT_LAYOUT_NO_ATOM;

    for (size_t Index = 0U; Index < AtomCount; Index++)
    {
        LayoutAtom* Atom = GetAtom(self, Index);
        switch (Atom->Kind)
        {
            case LayoutAtomKind_Break:
                Atom->Line = Line;
                Line++;
                LineWidth = 0.0f;
                HasContent = false;
                BreakSpace = TEXT_LAYOUT_NO_ATOM;
                break;
            case LayoutAtomKind_Space:
                Atom->Line = Line;
                LineWidth += Atom->Width;
                if (HasContent)
                {
                    BreakSpace = Index;
                }
                break;
            case LayoutAtomKind_Word:
            case LayoutAtomKind_Sprite:
                // Compared as the line would measure if it ended here: without the atom's trailing spacing.
                if (IsWrapping && (BreakSpace != TEXT_LAYOUT_NO_ATOM)
                    && ((LineWidth + Atom->Width - Atom->TrailingSpacing) > options->MaxWidth))
                {
                    // Drop the space run that ends at BreakSpace, and move everything after it to the next line.
                    for (size_t Space = BreakSpace + 1U; Space > 0U; Space--)
                    {
                        LayoutAtom* Candidate = GetAtom(self, Space - 1U);
                        if ((Candidate->Kind != LayoutAtomKind_Space) || (Candidate->Line != Line))
                        {
                            break;
                        }
                        Candidate->Line = TEXT_LAYOUT_LINE_DROPPED;
                    }
                    Line++;
                    LineWidth = 0.0f;
                    for (size_t Moved = BreakSpace + 1U; Moved < Index; Moved++)
                    {
                        LayoutAtom* MovedAtom = GetAtom(self, Moved);
                        MovedAtom->Line = Line;
                        LineWidth += MovedAtom->Width;
                    }
                    BreakSpace = TEXT_LAYOUT_NO_ATOM;
                }
                Atom->Line = Line;
                LineWidth += Atom->Width;
                HasContent = true;
                break;
        }
    }

    if (AtomCount == 0U)
    {
        return 0U;
    }
    bool EndsOnBreak = GetAtom(self, AtomCount - 1U)->Kind == LayoutAtomKind_Break;
    return (EndsOnBreak && !options->TracksOffsets) ? Line : (Line + 1U);
}

// Measures every line, applies shrinking and alignment, and sets the block size. A line is as wide as the end of its
// last glyph or sprite: its trailing spaces and the spacing after its last glyph do not count, so they neither shrink
// nor shift the line.
static Error MeasureLines(TextLayout* self, const TextLayoutOptions* options, size_t lineCount)
{
    LayoutLine Empty = { .Width = 0.0f, .Height = 0.0f, .Scale = 1.0f, .Top = 0.0f, .Left = 0.0f, .FirstCharacter = 0U, .CharacterCount = 0U };
    for (size_t Index = 0U; Index < lineCount; Index++)
    {
        if (!GenericBuffer_AddLast(&self->_lines, &Empty))
        {
            return GrowthError();
        }
    }

    // Atoms are in flow order, so each line's atoms are consecutive (dropped ones aside).
    size_t CurrentLine = TEXT_LAYOUT_LINE_DROPPED;
    float RunningWidth = 0.0f;
    for (size_t Index = 0U; Index < self->_atoms._count; Index++)
    {
        const LayoutAtom* Atom = GetAtom(self, Index);
        if ((Atom->Line == TEXT_LAYOUT_LINE_DROPPED) || (Atom->Line >= lineCount))
        {
            continue;
        }
        if (Atom->Line != CurrentLine)
        {
            CurrentLine = Atom->Line;
            RunningWidth = 0.0f;
        }
        LayoutLine* Line = GetLine(self, Atom->Line);
        if (Atom->Height > Line->Height)
        {
            Line->Height = Atom->Height;
        }
        RunningWidth += Atom->Width;
        if (Atom->Kind == LayoutAtomKind_Word)
        {
            Line->Width = Math_MaxFloat(0.0f, RunningWidth - Atom->TrailingSpacing);
        }
        else if (Atom->Kind == LayoutAtomKind_Sprite)
        {
            Line->Width = RunningWidth;
        }
    }
    // The empty line a tracked build gives a final line break is as tall as that break's text.
    const LayoutAtom* LastAtom = GetLastAtom(self);
    if ((lineCount > 0U) && (LastAtom != NULL) && (LastAtom->Kind == LayoutAtomKind_Break) && (LastAtom->Line == (lineCount - 2U)))
    {
        GetLine(self, lineCount - 1U)->Height = LastAtom->Height;
    }

    Vector2 BlockSize = { .x = 0.0f, .y = 0.0f };
    for (size_t Index = 0U; Index < lineCount; Index++)
    {
        LayoutLine* Line = GetLine(self, Index);
        if (options->HasMaxWidth && options->IsShrinkEnabled && (Line->Width > options->MaxWidth) && (Line->Width > 0.0f))
        {
            Line->Scale = options->MaxWidth / Line->Width;
        }
        Line->Top = BlockSize.y;
        BlockSize.y += Line->Height * Line->Scale;
        BlockSize.x = Math_MaxFloat(BlockSize.x, Line->Width * Line->Scale);
    }

    for (size_t Index = 0U; Index < lineCount; Index++)
    {
        LayoutLine* Line = GetLine(self, Index);
        float Slack = BlockSize.x - (Line->Width * Line->Scale);
        switch (options->Alignment)
        {
            case TextLayoutAlignment_Left:
                Line->Left = 0.0f;
                break;
            case TextLayoutAlignment_Center:
                Line->Left = Slack * 0.5f;
                break;
            case TextLayoutAlignment_Right:
                Line->Left = Slack;
                break;
        }
    }
    self->_size = BlockSize;
    return Error_CreateSuccess();
}


// Static functions: emitting runs.
static Error StartGlyphRun(TextLayout* self, const LayoutStyle* style, size_t styleIndex, size_t lineIndex, float x,
    float y, float scale)
{
    float FontSize = style->FontSize * scale;
    TextLayoutRun Run =
    {
        .Type = TextLayoutRunType_Glyphs,
        .X = x,
        .Y = y,
        .Width = 0.0f,
        .Height = FontSize,
        .StyleIndex = styleIndex,
        .Line = lineIndex,
        .Font = style->Font,
        .FontSize = FontSize,
        .Color = style->Color,
        .IsShadowActive = style->IsShadowActive,
        .ShadowColor = style->ShadowColor,
        .ShadowOffset = (Vector2){ .x = style->ShadowOffset.x * FontSize, .y = style->ShadowOffset.y * FontSize },
        .IsUnderlined = style->IsUnderlined,
        .IsStrikethrough = style->IsStrikethrough,
        .FirstGlyph = self->_glyphs._count,
        .GlyphCount = 0U,
        .DecorationWidth = 0.0f,
        .Instance = NULL,
        .SpriteColor = RenderColor_White()
    };
    return GenericBuffer_AddLast(&self->_runs, &Run) ? Error_CreateSuccess() : GrowthError();
}

// A tracked build's character, in flow order.
static Error AddCharacter(TextLayout* self, ScratchOffset place, size_t lineIndex, float x, float advance, TextLayoutCharacterKind kind)
{
    TextLayoutCharacter Character = { .ByteOffset = place.ByteOffset, .Line = (uint32_t)lineIndex, .X = x, .Advance = advance,
        .ByteCount = place.ByteCount, .Kind = (uint8_t)kind };
    self->_caretExtent = Math_MaxFloat(self->_caretExtent, x + advance);
    return GenericBuffer_AddLast(&self->_characters, &Character) ? Error_CreateSuccess() : GrowthError();
}

// The spaces a wrap dropped stand at the end of the line they followed, with no width: a caret before them is at that line's end.
static Error AddDroppedCharacters(TextLayout* self, const LayoutAtom* atom, size_t lineIndex, float penX)
{
    for (size_t Index = 0U; Index < atom->GlyphCount; Index++)
    {
        const ScratchOffset* Place = GenericBuffer_GetPointerToElement(&self->_scratchOffsets, atom->FirstGlyph + Index);
        Error Result = AddCharacter(self, *Place, lineIndex, penX, 0.0f, TextLayoutCharacterKind_Dropped);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

static Error EmitGlyphAtom(TextLayout* self, const LayoutAtom* atom, const LayoutLine* line, size_t lineIndex, bool isTracking, float* penX)
{
    const LayoutStyle* Style = GetStyle(self, atom->StyleIndex);
    float Scale = line->Scale;
    float Y = line->Top + ((line->Height - Style->FontSize) * Scale);

    TextLayoutRun* Run = (self->_runs._count > 0U) ? GenericBuffer_GetPointerToElement(&self->_runs, self->_runs._count - 1U)
        : NULL;
    // The last run continues when it holds this style on this line: anything in between (a sprite, another style, a
    // line break) would have started a run of its own.
    bool CanExtend = (Run != NULL) && (Run->Type == TextLayoutRunType_Glyphs) && (Run->StyleIndex == atom->StyleIndex)
        && (Run->Line == lineIndex);
    if (!CanExtend)
    {
        Error Result = StartGlyphRun(self, Style, atom->StyleIndex, lineIndex, *penX, Y, Scale);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        Run = GenericBuffer_GetPointerToElement(&self->_runs, self->_runs._count - 1U);
    }

    for (size_t Index = 0U; Index < atom->GlyphCount; Index++)
    {
        const ScratchGlyph* Source = GenericBuffer_GetPointerToElement(&self->_scratchGlyphs, atom->FirstGlyph + Index);
        if (Source->GlyphIndex >= 0)
        {
            TextLayoutGlyph Glyph = { .GlyphIndex = Source->GlyphIndex, .X = *penX };
            if (!GenericBuffer_AddLast(&self->_glyphs, &Glyph))
            {
                return GrowthError();
            }
            Run->GlyphCount++;
        }
        float Advance = Source->Advance * Scale;
        if (isTracking)
        {
            const ScratchOffset* Place = GenericBuffer_GetPointerToElement(&self->_scratchOffsets, atom->FirstGlyph + Index);
            Error Result = AddCharacter(self, *Place, lineIndex, *penX, Advance, TextLayoutCharacterKind_Shown);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
        *penX += Advance;
        Run->Width += Advance;
    }
    float End = Run->X + Run->Width;
    if (atom->Kind == LayoutAtomKind_Word)
    {
        End -= atom->TrailingSpacing * Scale;
    }
    float LineEnd = line->Left + (line->Width * Scale);
    Run->DecorationWidth = Math_MaxFloat(0.0f, Math_MinFloat(End, LineEnd) - Run->X);
    return Error_CreateSuccess();
}

static Error EmitSpriteAtom(TextLayout* self, const LayoutAtom* atom, const LayoutLine* line, size_t lineIndex, bool isTracking, float* penX)
{
    float Scale = line->Scale;
    TextLayoutRun Run =
    {
        .Type = TextLayoutRunType_Sprite,
        .X = *penX,
        .Y = line->Top + ((line->Height - atom->Height) * Scale),
        .Width = atom->Width * Scale,
        .Height = atom->Height * Scale,
        .StyleIndex = 0U,
        .Line = lineIndex,
        .Font = NULL,
        .FontSize = 0.0f,
        .Color = RenderColor_White(),
        .IsShadowActive = false,
        .ShadowColor = RenderColor_White(),
        .ShadowOffset = (Vector2){ .x = 0.0f, .y = 0.0f },
        .IsUnderlined = false,
        .IsStrikethrough = false,
        .FirstGlyph = 0U,
        .GlyphCount = 0U,
        .DecorationWidth = 0.0f,
        .Instance = atom->Instance,
        .SpriteColor = atom->SpriteColor
    };
    if (isTracking)
    {
        ScratchOffset Place = { .ByteOffset = atom->ByteOffset, .ByteCount = (uint8_t)TEXT_LAYOUT_OBJECT_CHARACTER_BYTES };
        Error Result = AddCharacter(self, Place, lineIndex, *penX, Run.Width, TextLayoutCharacterKind_Shown);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    *penX += Run.Width;
    return GenericBuffer_AddLast(&self->_runs, &Run) ? Error_CreateSuccess() : GrowthError();
}

static Error EmitRuns(TextLayout* self, bool isTracking)
{
    size_t CurrentLine = TEXT_LAYOUT_LINE_DROPPED;
    float PenX = 0.0f;
    for (size_t Index = 0U; Index < self->_atoms._count; Index++)
    {
        const LayoutAtom* Atom = GetAtom(self, Index);
        if (Atom->Line == TEXT_LAYOUT_LINE_DROPPED)
        {
            // Dropped spaces always follow content on a line, so a line is current.
            Error Result = (isTracking && (CurrentLine != TEXT_LAYOUT_LINE_DROPPED)) ? AddDroppedCharacters(self, Atom, CurrentLine, PenX)
                : Error_CreateSuccess();
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
            continue;
        }
        if (Atom->Line >= self->_lines._count)
        {
            continue;
        }
        const LayoutLine* Line = GetLine(self, Atom->Line);
        if (Atom->Line != CurrentLine)
        {
            CurrentLine = Atom->Line;
            PenX = Line->Left;
        }

        Error Result = Error_CreateSuccess();
        switch (Atom->Kind)
        {
            case LayoutAtomKind_Word:
            case LayoutAtomKind_Space:
                Result = EmitGlyphAtom(self, Atom, Line, CurrentLine, isTracking, &PenX);
                break;
            case LayoutAtomKind_Sprite:
                Result = EmitSpriteAtom(self, Atom, Line, CurrentLine, isTracking, &PenX);
                break;
            case LayoutAtomKind_Break:
                if (isTracking)
                {
                    ScratchOffset Place = { .ByteOffset = Atom->ByteOffset, .ByteCount = 1U };
                    Result = AddCharacter(self, Place, CurrentLine, PenX, 0.0f, TextLayoutCharacterKind_Break);
                }
                break;
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}

// Gives each line of a tracked build its characters: they are in flow order, and the lines rise along it.
static void AssignLineCharacters(TextLayout* self)
{
    size_t CharacterCount = self->_characters._count;
    size_t Next = 0U;
    for (size_t LineIndex = 0U; LineIndex < self->_lines._count; LineIndex++)
    {
        LayoutLine* Line = GetLine(self, LineIndex);
        Line->FirstCharacter = Next;
        while (Next < CharacterCount)
        {
            const TextLayoutCharacter* Character = GenericBuffer_GetPointerToElement(&self->_characters, Next);
            if ((size_t)Character->Line != LineIndex)
            {
                break;
            }
            Next++;
        }
        Line->CharacterCount = Next - Line->FirstCharacter;
    }
}


// Static functions: drawing.
static Vector2 PositionToPixels(RenderContext* context, RenderVector2D position)
{
    switch (position.Type)
    {
        case RenderValueType_NormalizedRelative:
            return RenderContext_VectorRelativeToPixel(context, position.Value);
        case RenderValueType_NormalizedFitted:
            return RenderContext_VectorFittedToPixel(context, position.Value, true);
        case RenderValueType_Pixel:
            return position.Value;
    }
    return position.Value;
}

/* Maps block-local fitted points to screen pixels: scale, then rotate around the origin, then anchor. */
typedef struct DrawTransformStruct
{
    RenderContext* Context;
    Vector2 OriginPixels;
    Vector2 BasePixels;
    float Cos;
    float Sin;
    float RotationRad;
    RenderColor Tint;
} DrawTransform;

static Vector2 PlaceFitted(const DrawTransform* transform, float x, float y)
{
    float OffsetX = RenderContext_SizeFittedToPixel(transform->Context, x) - transform->OriginPixels.x;
    float OffsetY = RenderContext_SizeFittedToPixel(transform->Context, y) - transform->OriginPixels.y;
    return (Vector2)
    {
        .x = transform->BasePixels.x + ((OffsetX * transform->Cos) - (OffsetY * transform->Sin)),
        .y = transform->BasePixels.y + ((OffsetX * transform->Sin) + (OffsetY * transform->Cos))
    };
}

/* Whether a run's box, widened by its reach and its shadow, meets the cull box. */
static bool IsRunInCullBox(const TextLayoutRun* run, Rectangle cullBox)
{
    float Reach = (run->Type == TextLayoutRunType_Glyphs) ? (run->FontSize * TEXT_LAYOUT_CULL_REACH_FONT_SIZES) : 0.0f;
    bool HasShadow = (run->Type == TextLayoutRunType_Glyphs) && run->IsShadowActive;
    float ShadowX = HasShadow ? Math_AbsFloat(run->ShadowOffset.x) : 0.0f;
    float ShadowY = HasShadow ? Math_AbsFloat(run->ShadowOffset.y) : 0.0f;
    float Left = run->X - Reach - ShadowX;
    float Right = run->X + run->Width + Reach + ShadowX;
    float Top = run->Y - Reach - ShadowY;
    float Bottom = run->Y + run->Height + Reach + ShadowY;
    return (Right >= cullBox.x) && (Left <= (cullBox.x + cullBox.width)) && (Bottom >= cullBox.y) && (Top <= (cullBox.y + cullBox.height));
}

static void DrawGlyphs(const TextLayout* self, const DrawTransform* transform, const TextLayoutRun* run, Vector2 offset,
    RenderColor color, const Rectangle* cullBox)
{
    const TextLayoutGlyph* Glyphs = GetGlyphs(self);
    float Reach = run->FontSize * TEXT_LAYOUT_CULL_REACH_FONT_SIZES;
    float CullLeft = (cullBox != NULL) ? (cullBox->x - Reach - Math_AbsFloat(offset.x)) : 0.0f;
    float CullRight = (cullBox != NULL) ? (cullBox->x + cullBox->width + Reach + Math_AbsFloat(offset.x)) : 0.0f;
    GlyphRenderArguments Arguments =
    {
        .TargetFont = &run->Font->_rayFont,
        .GlyphIndex = 0,
        .PenPosition = (Vector2){ .x = 0.0f, .y = 0.0f },
        .FontSizePixels = RenderContext_SizeFittedToPixel(transform->Context, run->FontSize),
        .RotationRad = transform->RotationRad,
        .TargetColor = MultiplyRenderColor(color, transform->Tint)
    };
    for (size_t Index = 0U; Index < run->GlyphCount; Index++)
    {
        const TextLayoutGlyph* Glyph = &Glyphs[run->FirstGlyph + Index];
        if ((cullBox != NULL) && ((Glyph->X < CullLeft) || (Glyph->X > CullRight)))
        {
            continue;
        }
        Arguments.GlyphIndex = Glyph->GlyphIndex;
        Arguments.PenPosition = PlaceFitted(transform, Glyph->X + offset.x, run->Y + offset.y);
        RenderContext_RenderGlyph(transform->Context, &Arguments);
    }
}

static void DrawDecoration(const DrawTransform* transform, const TextLayoutRun* run, float y)
{
    LineRenderArguments Arguments = { 0 };
    Arguments.StartPosition = RenderVector2D_Window(PlaceFitted(transform, run->X, y));
    Arguments.EndPosition = RenderVector2D_Window(PlaceFitted(transform, run->X + run->DecorationWidth, y));
    Arguments.Thickness = RenderFloat_Window(RenderContext_SizeFittedToPixel(transform->Context,
        run->FontSize * TEXT_LAYOUT_DECORATION_THICKNESS));
    Arguments.TargetColor = MultiplyRenderColor(run->Color, transform->Tint);
    RenderContext_RenderLine(transform->Context, &Arguments);
}

static void DrawSprite(const DrawTransform* transform, const TextLayoutRun* run)
{
    if (run->Instance == NULL)
    {
        return;
    }
    // The document binds only animations with frames; the check keeps a draw from building an error every frame.
    if (SpriteAnimationInstance_GetFrameCount(run->Instance) == 0U)
    {
        return;
    }
    SpriteAnimationFrame Frame;
    Error FrameResult = SpriteAnimationInstance_GetCurrentFrame(run->Instance, &Frame);
    if (FrameResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&FrameResult);
        return;
    }
    float TextureWidth = (float)Frame._texture.width;
    float TextureHeight = (float)Frame._texture.height;
    if ((TextureWidth <= 0.0f) || (TextureHeight <= 0.0f))
    {
        return;
    }

    TextureRenderArguments Arguments = { 0 };
    Arguments.Texture = Frame._texture;
    Arguments.Position = RenderVector2D_Window(PlaceFitted(transform, run->X, run->Y));
    Arguments.RelativeSourceRectangle = (Rectangle)
    {
        .x = Frame._areaInTexture.x / TextureWidth,
        .y = Frame._areaInTexture.y / TextureHeight,
        .width = Frame._areaInTexture.width / TextureWidth,
        .height = Frame._areaInTexture.height / TextureHeight
    };
    Arguments.Size = RenderVector2D_Window((Vector2)
    {
        .x = RenderContext_SizeFittedToPixel(transform->Context, run->Width),
        .y = RenderContext_SizeFittedToPixel(transform->Context, run->Height)
    });
    Arguments.RelativeOrigin = (Vector2){ .x = 0.0f, .y = 0.0f };
    Arguments.RotationRad = transform->RotationRad;
    Arguments.TargetColor = MultiplyRenderColor(run->SpriteColor, transform->Tint);
    RenderContext_RenderTexture2D(transform->Context, &Arguments);
}


// Public functions: lifecycle.
Error TextLayout_Construct(TextLayout* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextLayout_Construct: self must not be NULL.");
    }
    GenericBuffer_AllocateVariable(&self->_runs, 0U, sizeof(TextLayoutRun));
    GenericBuffer_AllocateVariable(&self->_glyphs, 0U, sizeof(TextLayoutGlyph));
    GenericBuffer_AllocateVariable(&self->_lines, 0U, sizeof(LayoutLine));
    GenericBuffer_AllocateVariable(&self->_styles, 0U, sizeof(LayoutStyle));
    GenericBuffer_AllocateVariable(&self->_atoms, 0U, sizeof(LayoutAtom));
    GenericBuffer_AllocateVariable(&self->_scratchGlyphs, 0U, sizeof(ScratchGlyph));
    GenericBuffer_AllocateVariable(&self->_scratchOffsets, 0U, sizeof(ScratchOffset));
    GenericBuffer_AllocateVariable(&self->_flowText, 0U, sizeof(unsigned char));
    GenericBuffer_AllocateVariable(&self->_characters, 0U, sizeof(TextLayoutCharacter));
    self->_emptyLineHeight = 0.0f;
    self->_caretExtent = 0.0f;
    self->_size = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_isBuilt = false;
    self->_isWaitingForGlyphs = false;
    self->_document = NULL;
    self->_documentRevision = 0U;
    self->_treeRevision = 0U;
    self->_options = TextLayoutOptions_CreateDefault();
    return Error_CreateSuccess();
}

Error TextLayout_Deconstruct(TextLayout* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }
    GenericBuffer* Buffers[] = { &self->_runs, &self->_glyphs, &self->_lines, &self->_styles, &self->_atoms,
        &self->_scratchGlyphs, &self->_scratchOffsets, &self->_flowText, &self->_characters };
    for (size_t Index = 0U; Index < (sizeof(Buffers) / sizeof(Buffers[0])); Index++)
    {
        // Back to an empty buffer, so a second deconstruct or a stray query sees no elements.
        size_t ElementSize = Buffers[Index]->_elementSize;
        Memory_Free(Buffers[Index]->_data);
        GenericBuffer_AllocateVariable(Buffers[Index], 0U, ElementSize);
    }
    self->_isBuilt = false;
    self->_document = NULL;
    return Error_CreateSuccess();
}


// Public functions: building.
Error TextLayout_Build(TextLayout* self, TextDocument* document, const TextLayoutOptions* options)
{
    if ((self == NULL) || (document == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextLayout_Build: self, document and options must not be NULL.");
    }
    ClearBuild(self);
    Error Result = ValidateOptions(options);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    TextComponent* Root = TextDocument_GetRoot(document);
    if (Root != NULL)
    {
        BuildContext Context = { .Layout = self, .Document = document, .Options = options, .IsTracking = options->TracksOffsets };
        Result = Flatten(&Context, Root);
        if (Result.Code == ErrorCode_Success)
        {
            size_t LineCount = AssignLines(self, options);
            Result = MeasureLines(self, options, LineCount);
        }
        if (Result.Code == ErrorCode_Success)
        {
            Result = EmitRuns(self, options->TracksOffsets);
        }
        if ((Result.Code == ErrorCode_Success) && options->TracksOffsets)
        {
            AssignLineCharacters(self);
        }
        if (Result.Code != ErrorCode_Success)
        {
            ClearBuild(self);
            return Result;
        }
    }

    self->_isBuilt = true;
    self->_document = document;
    self->_documentRevision = TextDocument_GetRevision(document);
    self->_treeRevision = (Root != NULL) ? TextComponent_GetRevision(Root) : 0U;
    self->_options = *options;
    return Error_CreateSuccess();
}

bool TextLayout_IsUpToDate(const TextLayout* self, const TextDocument* document, const TextLayoutOptions* options)
{
    if (!self->_isBuilt || self->_isWaitingForGlyphs || (self->_document != document)
        || (self->_documentRevision != TextDocument_GetRevision(document)))
    {
        return false;
    }
    const TextComponent* Root = TextDocument_GetRoot(document);
    uint64_t TreeRevision = (Root != NULL) ? TextComponent_GetRevision(Root) : 0U;
    return (TreeRevision == self->_treeRevision) && AreOptionsEqual(&self->_options, options);
}

Error TextLayout_Refresh(TextLayout* self, TextDocument* document, const TextLayoutOptions* options)
{
    if ((self == NULL) || (document == NULL) || (options == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "TextLayout_Refresh: self, document and options must not be NULL.");
    }
    if (TextLayout_IsUpToDate(self, document, options))
    {
        return Error_CreateSuccess();
    }
    return TextLayout_Build(self, document, options);
}


Rectangle TextLayout_GetLineBounds(const TextLayout* self, size_t index)
{
    if (index >= self->_lines._count)
    {
        return (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    }
    // The draw path's const access applies here too: the buffer holds LayoutLine elements.
    const LayoutLine* Line = (const LayoutLine*)self->_lines._data + index;
    return (Rectangle){ .x = Line->Left, .y = Line->Top, .width = Line->Width * Line->Scale,
        .height = Line->Height * Line->Scale };
}

Rectangle TextLayout_GetRunBounds(const TextLayout* self, size_t index)
{
    if (index >= self->_runs._count)
    {
        return (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    }
    const TextLayoutRun* Run = &GetRuns(self)[index];
    return (Rectangle){ .x = Run->X, .y = Run->Y, .width = Run->Width, .height = Run->Height };
}

Vector2 TextLayout_GetRunShadowOffset(const TextLayout* self, size_t index)
{
    if (index >= self->_runs._count)
    {
        return (Vector2){ .x = 0.0f, .y = 0.0f };
    }
    const TextLayoutRun* Run = &GetRuns(self)[index];
    bool HasShadow = (Run->Type == TextLayoutRunType_Glyphs) && Run->IsShadowActive;
    return HasShadow ? Run->ShadowOffset : (Vector2){ .x = 0.0f, .y = 0.0f };
}


// Public functions: carets.
static const TextLayoutCharacter* GetCharacters(const TextLayout* self)
{
    return (const TextLayoutCharacter*)self->_characters._data;
}

static const LayoutLine* GetConstLine(const TextLayout* self, size_t index)
{
    return (const LayoutLine*)self->_lines._data + index;
}

/* The first character whose bytes start at or after @p byteOffset; the character count when none does. */
static size_t FindCharacterAtOrAfter(const TextLayout* self, size_t byteOffset)
{
    const TextLayoutCharacter* Characters = GetCharacters(self);
    size_t Low = 0U;
    size_t High = self->_characters._count;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        if ((size_t)Characters[Middle].ByteOffset < byteOffset)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return Low;
}

/* Whether the text ends with the empty line a final line break has. */
static bool HasTrailingEmptyLine(const TextLayout* self)
{
    size_t LineCount = self->_lines._count;
    return (LineCount > 0U) && (GetConstLine(self, LineCount - 1U)->CharacterCount == 0U);
}

/* Where a line's end shows: after its last character, which is its break or a space a wrap dropped when it has one (they stand at the
 * end of what shows, with no width); an empty final line's at its left edge. */
static float GetLineEndX(const TextLayout* self, size_t line)
{
    const LayoutLine* Line = GetConstLine(self, line);
    if (Line->CharacterCount == 0U)
    {
        return Line->Left;
    }
    const TextLayoutCharacter* Last = &GetCharacters(self)[Line->FirstCharacter + Line->CharacterCount - 1U];
    return Last->X + Last->Advance;
}

/* Where a caret at an offset stands: its line, its x, and how far the character after it reaches. */
static void LocateCaret(const TextLayout* self, size_t byteOffset, size_t* outLine, float* outX, float* outWidth)
{
    *outLine = 0U;
    *outX = 0.0f;
    *outWidth = 0.0f;
    size_t CharacterCount = self->_characters._count;
    if (CharacterCount == 0U)
    {
        if (self->_lines._count > 0U)
        {
            *outLine = self->_lines._count - 1U;
            *outX = GetConstLine(self, *outLine)->Left;
        }
        return;
    }
    size_t Index = FindCharacterAtOrAfter(self, byteOffset);
    if (Index < CharacterCount)
    {
        const TextLayoutCharacter* Character = &GetCharacters(self)[Index];
        *outLine = (size_t)Character->Line;
        *outX = Character->X;
        *outWidth = Character->Advance; // A break's and a dropped space's are none.
        return;
    }
    // Past the last character: after it, or on the empty line a final break has.
    if (HasTrailingEmptyLine(self))
    {
        *outLine = self->_lines._count - 1U;
        *outX = GetConstLine(self, *outLine)->Left;
        return;
    }
    const TextLayoutCharacter* Last = &GetCharacters(self)[CharacterCount - 1U];
    *outLine = (size_t)Last->Line;
    *outX = Last->X + Last->Advance;
}

/* The line's start and end as shown; for a line out of range, the text's end. */
static void GetLineSpan(const TextLayout* self, size_t line, size_t* outStart, size_t* outEnd)
{
    size_t FlowLength = self->_flowText._count;
    *outStart = FlowLength;
    *outEnd = FlowLength;
    if (line >= self->_lines._count)
    {
        return;
    }
    const LayoutLine* Line = GetConstLine(self, line);
    const TextLayoutCharacter* Characters = GetCharacters(self);
    if (Line->CharacterCount == 0U)
    {
        return;
    }
    *outStart = (size_t)Characters[Line->FirstCharacter].ByteOffset;
    for (size_t Index = 0U; Index < Line->CharacterCount; Index++)
    {
        const TextLayoutCharacter* Character = &Characters[Line->FirstCharacter + Index];
        if (Character->Kind != (uint8_t)TextLayoutCharacterKind_Shown)
        {
            *outEnd = (size_t)Character->ByteOffset;
            return;
        }
    }
    const TextLayoutCharacter* Last = &Characters[Line->FirstCharacter + Line->CharacterCount - 1U];
    *outEnd = (size_t)Last->ByteOffset + (size_t)Last->ByteCount;
}

/* Where a caret at an offset stands on a given line: the line's end for an offset at or past it, its start for one before it. */
static float GetCaretXOnLine(const TextLayout* self, size_t line, size_t byteOffset)
{
    size_t Start = 0U;
    size_t End = 0U;
    GetLineSpan(self, line, &Start, &End);
    if (byteOffset >= End)
    {
        return GetLineEndX(self, line);
    }
    const LayoutLine* Line = GetConstLine(self, line);
    if ((byteOffset <= Start) || (Line->CharacterCount == 0U))
    {
        return (Line->CharacterCount > 0U) ? GetCharacters(self)[Line->FirstCharacter].X : Line->Left;
    }
    size_t Index = FindCharacterAtOrAfter(self, byteOffset);
    return (Index < self->_characters._count) ? GetCharacters(self)[Index].X : GetLineEndX(self, line);
}

const unsigned char* TextLayout_GetFlowText(const TextLayout* self, size_t* outLength)
{
    bool IsTracked = TextLayout_HasOffsets(self);
    if (outLength != NULL)
    {
        *outLength = IsTracked ? self->_flowText._count : 0U;
    }
    return (IsTracked && (self->_flowText._data != NULL)) ? (const unsigned char*)self->_flowText._data : (const unsigned char*)u8"";
}

bool TextLayout_GetCaretBox(const TextLayout* self, size_t byteOffset, Rectangle* outBox)
{
    *outBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    if (!TextLayout_HasOffsets(self))
    {
        return false;
    }
    if (self->_lines._count == 0U)
    {
        outBox->height = self->_emptyLineHeight;
        return true;
    }
    size_t Line = 0U;
    float X = 0.0f;
    float Width = 0.0f;
    LocateCaret(self, byteOffset, &Line, &X, &Width);
    const LayoutLine* Shown = GetConstLine(self, Line);
    *outBox = (Rectangle){ .x = X, .y = Shown->Top, .width = Width, .height = Shown->Height * Shown->Scale };
    return true;
}

size_t TextLayout_GetLineAtOffset(const TextLayout* self, size_t byteOffset)
{
    if (!TextLayout_HasOffsets(self) || (self->_lines._count == 0U))
    {
        return 0U;
    }
    size_t Line = 0U;
    float X = 0.0f;
    float Width = 0.0f;
    LocateCaret(self, byteOffset, &Line, &X, &Width);
    return Line;
}

bool TextLayout_GetLineOffsets(const TextLayout* self, size_t line, size_t* outStart, size_t* outEnd)
{
    *outStart = 0U;
    *outEnd = 0U;
    if (!TextLayout_HasOffsets(self) || (line >= self->_lines._count))
    {
        return false;
    }
    GetLineSpan(self, line, outStart, outEnd);
    return true;
}

size_t TextLayout_GetOffsetOnLine(const TextLayout* self, size_t line, float x)
{
    if (!TextLayout_HasOffsets(self) || (self->_lines._count == 0U))
    {
        return 0U;
    }
    size_t Clamped = (line < self->_lines._count) ? line : (self->_lines._count - 1U);
    const LayoutLine* Line = GetConstLine(self, Clamped);
    const TextLayoutCharacter* Characters = GetCharacters(self);
    for (size_t Index = 0U; Index < Line->CharacterCount; Index++)
    {
        const TextLayoutCharacter* Character = &Characters[Line->FirstCharacter + Index];
        if (Character->Kind != (uint8_t)TextLayoutCharacterKind_Shown)
        {
            break;
        }
        if (x < (Character->X + (Character->Advance * 0.5f)))
        {
            return (size_t)Character->ByteOffset;
        }
    }
    size_t Start = 0U;
    size_t End = 0U;
    GetLineSpan(self, Clamped, &Start, &End);
    return End;
}

bool TextLayout_HitTest(const TextLayout* self, Vector2 point, size_t* outByteOffset)
{
    *outByteOffset = 0U;
    if (!TextLayout_HasOffsets(self))
    {
        return false;
    }
    if (self->_lines._count == 0U)
    {
        return true;
    }
    *outByteOffset = TextLayout_GetOffsetOnLine(self, TextLayout_GetLineAtY(self, point.y), point.x);
    return true;
}

size_t TextLayout_GetLineAtY(const TextLayout* self, float y)
{
    size_t LineCount = self->_lines._count;
    if (LineCount == 0U)
    {
        return 0U;
    }
    // The lines are in order from the top: the first whose bottom lies below the height, else the last.
    size_t Low = 0U;
    size_t High = LineCount - 1U;
    while (Low < High)
    {
        size_t Middle = Low + ((High - Low) / 2U);
        const LayoutLine* Line = GetConstLine(self, Middle);
        if ((Line->Top + (Line->Height * Line->Scale)) <= y)
        {
            Low = Middle + 1U;
        }
        else
        {
            High = Middle;
        }
    }
    return Low;
}

bool TextLayout_GetRangeBoxOnLine(const TextLayout* self, size_t line, size_t start, size_t end, Rectangle* outBox)
{
    *outBox = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    if (!TextLayout_HasOffsets(self) || (line >= self->_lines._count) || (end <= start))
    {
        return false;
    }
    size_t LineStart = 0U;
    size_t LineEnd = 0U;
    GetLineSpan(self, line, &LineStart, &LineEnd);
    // An empty final line holds nothing a range could cover.
    bool IsEmptyFinal = GetConstLine(self, line)->CharacterCount == 0U;
    if (IsEmptyFinal || (start > LineEnd) || (end <= LineStart))
    {
        return false;
    }
    float Left = GetCaretXOnLine(self, line, start);
    float Right = GetCaretXOnLine(self, line, end);
    const LayoutLine* Shown = GetConstLine(self, line);
    *outBox = (Rectangle){ .x = Left, .y = Shown->Top, .width = Math_MaxFloat(0.0f, Right - Left), .height = Shown->Height * Shown->Scale };
    return true;
}


// Public functions: drawing.
Error TextLayout_Draw(const TextLayout* self, RenderContext* context, const TextLayoutDrawArguments* args)
{
    if ((self == NULL) || (context == NULL) || (args == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "TextLayout_Draw: self, context and args must not be NULL.");
    }
    size_t RunCount = self->_runs._count;
    if (!self->_isBuilt || (RunCount == 0U))
    {
        return Error_CreateSuccess();
    }

    DrawTransform Placement =
    {
        .Context = context,
        .OriginPixels = (Vector2)
        {
            .x = RenderContext_SizeFittedToPixel(context, self->_size.x) * args->RelativeOrigin.x,
            .y = RenderContext_SizeFittedToPixel(context, self->_size.y) * args->RelativeOrigin.y
        },
        .BasePixels = PositionToPixels(context, args->Position),
        .Cos = Math_CosFloat(args->RotationRad),
        .Sin = Math_SinFloat(args->RotationRad),
        .RotationRad = args->RotationRad,
        .Tint = args->Tint
    };

    // Through the context's clip stack, so the cut nests inside any outer clip (a parent widget's) and restores it.
    if (args->HasScissor)
    {
        Vector2 Start = RenderContext_VectorRelativeToPixel(context,
            (Vector2){ .x = args->ScissorRelative.x, .y = args->ScissorRelative.y });
        Vector2 Size = RenderContext_VectorRelativeToPixel(context,
            (Vector2){ .x = args->ScissorRelative.width, .y = args->ScissorRelative.height });
        RenderContext_PushClip(context, (Rectangle){ .x = Start.x, .y = Start.y, .width = Size.x, .height = Size.y });
    }

    const TextLayoutRun* Runs = GetRuns(self);
    const Rectangle* CullBox = args->HasCullBox ? &args->CullBox : NULL;
    // Every shadow first, so no shadow covers the text of an earlier run.
    for (size_t Index = 0U; Index < RunCount; Index++)
    {
        const TextLayoutRun* Run = &Runs[Index];
        if ((Run->Type == TextLayoutRunType_Glyphs) && Run->IsShadowActive && ((CullBox == NULL) || IsRunInCullBox(Run, *CullBox)))
        {
            DrawGlyphs(self, &Placement, Run, Run->ShadowOffset, Run->ShadowColor, CullBox);
        }
    }
    for (size_t Index = 0U; Index < RunCount; Index++)
    {
        const TextLayoutRun* Run = &Runs[Index];
        if ((CullBox != NULL) && !IsRunInCullBox(Run, *CullBox))
        {
            continue;
        }
        switch (Run->Type)
        {
            case TextLayoutRunType_Glyphs:
                DrawGlyphs(self, &Placement, Run, (Vector2){ .x = 0.0f, .y = 0.0f }, Run->Color, CullBox);
                if (Run->IsUnderlined)
                {
                    DrawDecoration(&Placement, Run, Run->Y + Run->Height);
                }
                if (Run->IsStrikethrough)
                {
                    DrawDecoration(&Placement, Run, Run->Y + (Run->Height * TEXT_LAYOUT_STRIKETHROUGH_HEIGHT));
                }
                break;
            case TextLayoutRunType_Sprite:
                DrawSprite(&Placement, Run);
                break;
        }
    }

    if (args->HasScissor)
    {
        RenderContext_PopClip(context);
    }
    return Error_CreateSuccess();
}
