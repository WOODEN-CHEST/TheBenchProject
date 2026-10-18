#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector2/Rectangle come from Raylib and RenderColor/RenderVector2D/RenderContext from the renderer: they are part
// of the options and draw arguments. The layout embeds its buffers by value.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "TextDocument.h"
#include "wr/WRMemory.h"
#include "wr/WRError.h"


/**
 * @file TextLayout.h
 * @brief Measures and arranges a TextDocument's text once, then draws the result as often as needed.
 *
 * FLOW. A component contributes its own content first, then each child in order, recursively, as one inline flow:
 * the text of string components (a translate component shows its translation, else its fallback, else its key) and
 * the box of sprite components (a sprite always takes its size, even when its animation is missing). '\n' ends a
 * line; a final '\n' does not add an empty line after it. A tab is a gap TEXT_LAYOUT_TAB_SPACE_COUNT spaces wide;
 * other control characters (C0, DEL, C1) are skipped. Within a line, shorter runs are aligned to the line's BOTTOM.
 *
 * BOUNDS. A line is as wide as the end of its last glyph or sprite: trailing spaces and the spacing after the last
 * glyph do not count. Spaces at the start of a line (of the text, or after '\n') are kept. With a maximum width,
 * wrapping breaks a line at the last space (or tab) that follows content on it, and the spaces at the break are
 * dropped; a word that does not fit on its own line still overflows. Shrinking then scales every line still wider than
 * the maximum down to fit it (the whole line, glyphs and sprites alike). Lines are aligned left, centered or right
 * within the block's width (the widest line). Underline and strikethrough cover a run's glyphs and the spaces between
 * them, never past the line's last glyph.
 *
 * UNITS. Everything is in normalized-fitted units, where 1.0 is the height of the render target area, on both axes:
 * a string component's size is its line height and a sprite's size is its box. The options' size multiplier scales
 * every component size (and the maximum width is compared after it). Drawing converts to pixels.
 *
 * GLYPHS. Every glyph is resolved once, when the layout is built: through the document's font binding to a glyph
 * index (a codepoint the font lacks draws as '?'). A glyph advances by its font advance plus the component's spacing
 * (never less than nothing, so a negative spacing at most stacks glyphs). A shadow is drawn at the component's shadow
 * offset, or at its font's (GameFont_GetShadowOffset) when it sets none, both relative to the size.
 *
 * BUILD IN UPDATE, DRAW IN RENDER. Building resolves names through the document, which may load assets, so build in
 * update code (after the window exists) and only draw in render code. TextLayout_Refresh rebuilds only when the
 * layout is out of date: when another document is given, the document's revision or its tree's revision changed
 * (any edit inside the tree), the options changed, or the last build used a font without glyphs (raylib's built-in
 * font before InitWindow). Sprite frames are read when drawing, so animation needs no rebuild.
 *
 * LIFETIME. A built layout points at fonts and animation instances its document owns: never draw it after the
 * document is destroyed. Construct with TextLayout_Construct and release with TextLayout_Deconstruct; reuse one
 * layout across builds (its buffers keep their capacity). Not thread-safe.
 */


// Macros.
/** @brief Width of a tab, in spaces of its component's font. */
#define TEXT_LAYOUT_TAB_SPACE_COUNT (4)
/** @brief Largest accepted size multiplier (TextLayoutOptions::SizeMultiplier). */
#define TEXT_LAYOUT_SIZE_MULTIPLIER_MAX (100.0f)


// Types.
/**
 * @brief Horizontal alignment of every line within the block.
 */
typedef enum TextLayoutAlignmentEnum
{
    /** @brief Lines start at the block's left edge. */
    TextLayoutAlignment_Left,
    /** @brief Lines are centered within the block's width. */
    TextLayoutAlignment_Center,
    /** @brief Lines end at the block's right edge. */
    TextLayoutAlignment_Right
} TextLayoutAlignment;

/**
 * @brief How to lay a document out; a struct so new options can be added without breaking callers.
 *
 * Create with TextLayoutOptions_CreateDefault and change what you need.
 */
typedef struct TextLayoutOptionsStruct
{
    /** @brief Multiplies every component size; finite and within [0, TEXT_LAYOUT_SIZE_MULTIPLIER_MAX]. */
    float SizeMultiplier;
    /** @brief Whether MaxWidth constrains the lines (for wrapping and shrinking). */
    bool HasMaxWidth;
    /** @brief The widest a line should be, in normalized-fitted units (after the multiplier); finite and >= 0. */
    float MaxWidth;
    /** @brief Wrap lines at spaces where they would be wider than MaxWidth (needs HasMaxWidth). */
    bool IsWrapEnabled;
    /** @brief Shrink every line still wider than MaxWidth to fit it (needs HasMaxWidth); applied after wrapping. */
    bool IsShrinkEnabled;
    /** @brief Horizontal alignment of the lines. */
    TextLayoutAlignment Alignment;
} TextLayoutOptions;

/**
 * @brief Where and how to draw a built layout; a struct so new options can be added without breaking callers.
 *
 * Create with TextLayoutDrawArguments_Create and change what you need.
 */
typedef struct TextLayoutDrawArgumentsStruct
{
    /** @brief Where the block's origin (see @c RelativeOrigin) is placed. */
    RenderVector2D Position;
    /** @brief The origin within the block's size, normalized [0;1]; also the rotation pivot. */
    Vector2 RelativeOrigin;
    /** @brief Rotation of the whole block around the origin, in radians. */
    float RotationRad;
    /** @brief Multiplied into every color. */
    RenderColor Tint;
    /** @brief true to clip drawing to @c ScissorRelative. */
    bool HasScissor;
    /** @brief Clip rectangle in screen-relative [0;1] coordinates (x, y, width, height); used when @c HasScissor. It
     *         is pushed on the render context's clip stack, so it nests inside any outer clip (RenderContext_PushClip). */
    Rectangle ScissorRelative;
} TextLayoutDrawArguments;

/**
 * @brief A built layout: glyph and sprite runs with their positions, plus what it was built from. All fields are
 *        internal; use the TextLayout_* functions.
 */
typedef struct TextLayoutStruct
{
    /** @brief The laid-out runs (private element type): styled glyph spans and sprite boxes. */
    GenericBuffer _runs;
    /** @brief The drawable glyphs of every glyph run (private element type). */
    GenericBuffer _glyphs;
    /** @brief The laid-out lines (private element type). */
    GenericBuffer _lines;
    /** @brief Build scratch: one style per text-bearing component (private element type). */
    GenericBuffer _styles;
    /** @brief Build scratch: words, spaces, sprites and line breaks (private element type). */
    GenericBuffer _atoms;
    /** @brief Build scratch: every resolved glyph with its advance (private element type). */
    GenericBuffer _scratchGlyphs;
    /** @brief The block's size in normalized-fitted units. */
    Vector2 _size;
    /** @brief Whether the layout holds a build. */
    bool _isBuilt;
    /** @brief Whether the build used a font without glyphs (raylib's built-in font before InitWindow): it is then out
     *         of date until the font has them. */
    bool _isWaitingForGlyphs;
    /** @brief The document it was built from; borrowed, only compared. */
    const TextDocument* _document;
    /** @brief The document's revision at the build. */
    uint64_t _documentRevision;
    /** @brief The tree root's revision at the build (0 for an empty document). */
    uint64_t _treeRevision;
    /** @brief The options of the build. */
    TextLayoutOptions _options;
} TextLayout;


// Helpers.
/**
 * @brief Default layout options: multiplier 1, no maximum width, left alignment.
 * @returns The options.
 */
static inline TextLayoutOptions TextLayoutOptions_CreateDefault(void)
{
    return (TextLayoutOptions)
    {
        .SizeMultiplier = 1.0f,
        .HasMaxWidth = false,
        .MaxWidth = 0.0f,
        .IsWrapEnabled = false,
        .IsShrinkEnabled = false,
        .Alignment = TextLayoutAlignment_Left
    };
}

/**
 * @brief Default draw arguments: top-left origin, no rotation, white tint, no scissor.
 * @param position Where to place the block's top-left corner.
 * @returns The arguments.
 */
static inline TextLayoutDrawArguments TextLayoutDrawArguments_Create(RenderVector2D position)
{
    return (TextLayoutDrawArguments)
    {
        .Position = position,
        .RelativeOrigin = (Vector2){ .x = 0.0f, .y = 0.0f },
        .RotationRad = 0.0f,
        .Tint = RenderColor_White(),
        .HasScissor = false,
        .ScissorRelative = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f }
    };
}


// Lifecycle.
/**
 * @brief Initializes an empty layout (nothing is allocated until the first build).
 * @param self The layout to initialize; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error TextLayout_Construct(TextLayout* self);

/**
 * @brief Releases the layout's buffers. Safe on NULL.
 * @param self The layout, or NULL.
 * @returns Success.
 */
Error TextLayout_Deconstruct(TextLayout* self);


// Building.
/**
 * @brief Lays out @p document's tree with @p options, replacing any previous build.
 *
 * Resolves fonts and sprite animations through the document (which may load assets: call from update code).
 * O(characters) plus one font lookup per character (O(log glyphs)).
 * @param self The layout; must not be NULL.
 * @param document The document to lay out; must not be NULL and must outlive the build.
 * @param options The options; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL or the alignment is unknown;
 *          ErrorCode_ArgumentOutOfRange if the multiplier or the maximum width is out of range;
 *          ErrorCode_BufferTooLarge if a buffer could not grow. On failure the layout is left empty (not built).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextLayout_Build(TextLayout* self, TextDocument* document, const TextLayoutOptions* options);

/**
 * @brief Reports whether the layout's build still matches @p document and @p options (see the file comment).
 * @param self The layout; must not be NULL.
 * @param document The document; must not be NULL.
 * @param options The options; must not be NULL.
 * @returns true if the layout is built from this document, at its current revisions, with equal options.
 */
bool TextLayout_IsUpToDate(const TextLayout* self, const TextDocument* document, const TextLayoutOptions* options);

/**
 * @brief Builds the layout only when it is not up to date (TextLayout_IsUpToDate); call it every update tick.
 * @param self The layout; must not be NULL.
 * @param document The document to lay out; must not be NULL.
 * @param options The options; must not be NULL.
 * @returns Success (also when nothing had to be done); otherwise as TextLayout_Build.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TextLayout_Refresh(TextLayout* self, TextDocument* document, const TextLayoutOptions* options);

/**
 * @brief Returns the block's size in normalized-fitted units ((0, 0) when not built or empty).
 * @param self The layout; must not be NULL.
 * @returns The size.
 */
static inline Vector2 TextLayout_GetSize(const TextLayout* self)
{
    return self->_size;
}

/**
 * @brief Returns the number of laid-out lines (0 when not built or empty).
 * @param self The layout; must not be NULL.
 * @returns The line count.
 */
static inline size_t TextLayout_GetLineCount(const TextLayout* self)
{
    return self->_lines._count;
}

/**
 * @brief Returns the box of line @p index: its alignment offset, top, width and height after shrinking, in
 *        block-local normalized-fitted units (the block's top-left is (0, 0)).
 * @param self The layout; must not be NULL.
 * @param index The line; must be less than TextLayout_GetLineCount.
 * @returns The box, or an empty rectangle for an index out of range.
 */
Rectangle TextLayout_GetLineBounds(const TextLayout* self, size_t index);

/**
 * @brief Returns the number of laid-out runs: spans of glyphs that share a component and a line, and sprite boxes.
 * @param self The layout; must not be NULL.
 * @returns The run count (0 when not built or empty).
 */
static inline size_t TextLayout_GetRunCount(const TextLayout* self)
{
    return self->_runs._count;
}

/**
 * @brief Returns the box of run @p index in block-local normalized-fitted units (for hit-testing text).
 * @param self The layout; must not be NULL.
 * @param index The run; must be less than TextLayout_GetRunCount.
 * @returns The box, or an empty rectangle for an index out of range.
 */
Rectangle TextLayout_GetRunBounds(const TextLayout* self, size_t index);

/**
 * @brief Returns how far run @p index's shadow is drawn from its glyphs, in normalized-fitted units: the component's
 *        shadow offset, or its font's when it sets none, times the run's size. The shadow covers the run's box moved
 *        by this much, so drawn text reaches beyond TextLayout_GetRunBounds by it.
 * @param self The layout; must not be NULL.
 * @param index The run; must be less than TextLayout_GetRunCount.
 * @returns The offset; (0, 0) for a run without a shadow (a sprite run, an inactive shadow) or an index out of range.
 */
Vector2 TextLayout_GetRunShadowOffset(const TextLayout* self, size_t index);

/**
 * @brief Reports whether the layout holds a build.
 * @param self The layout; must not be NULL.
 * @returns true after a successful TextLayout_Build.
 */
static inline bool TextLayout_IsBuilt(const TextLayout* self)
{
    return self->_isBuilt;
}


// Drawing.
/**
 * @brief Draws the built layout through the render context (shadows first, then text, decorations and sprites).
 *
 * Reads the current frame of every sprite's animation instance; changes nothing. Draws nothing when the layout is
 * not built. Does not begin or end a render pass.
 * @param self The layout; must not be NULL. Its document must still exist.
 * @param context The render context; must not be NULL.
 * @param args The draw arguments; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL.
 */
Error TextLayout_Draw(const TextLayout* self, RenderContext* context, const TextLayoutDrawArguments* args);
