#pragma once
#include <stdbool.h>
#include "raylib/raylib.h"
#include "Renderer.h"
#include "TextComponent.h"
#include "wr/WRMemory.h"
#include "wr/WRError.h"


/**
 * @file TextComponentRenderer.h
 * @brief Centralized measuring and drawing of text component trees.
 *
 * Rendering a component is like an advanced "draw text" call: it flattens the component and its whole
 * subtree into an inline run of glyphs and sprites, lays them out (left to right, breaking on newlines),
 * and draws them through a RenderContext. It never touches the render target or shaders itself.
 *
 * LAYOUT. A component contributes its own content first, then each child in order, recursively, forming
 * one continuous inline flow. Newlines in string content start new lines and increase the height. Within
 * a line, runs shorter than the line's tallest run are aligned to the line's BOTTOM.
 *
 * UNITS. All component sizes and the measured draw size are expressed in normalized-fitted units where
 * 1.0 equals the full height of the render target area, for BOTH axes (so string sizes and sprite sizes
 * compose on a common scale). A string component's size is its line height; a sprite component's size is
 * its box, both as fractions of the target-area height. Measurement is independent of the render context;
 * drawing uses the context to convert these units to pixels.
 *
 * SHADOWS. A string component's shadow is the same text drawn behind it at an offset relative to the
 * component size; shadows are excluded from measurement and from the origin. The default shadow color is
 * the component color with its brightness reduced.
 *
 * The renderer keeps small reusable scratch buffers, so construct one and reuse it across frames.
 */


// Types.
/**
 * @brief Parameters for drawing a component; a struct so new options can be added without breaking callers.
 */
typedef struct ComponentRenderArgumentsStruct
{
    /** @brief Where the component's origin (see @c RelativeOrigin) is placed. */
    RenderVector2D Position;
    /** @brief Extra multiplier applied on top of every component's own size (1.0 leaves sizes unchanged). */
    float AdditionalSizeMultiplier;
    /** @brief Tint multiplied into every component's own color. */
    RenderColor Tint;
    /** @brief Rotation of the whole composed block around the origin, in radians. */
    float RotationRad;
    /** @brief Origin within the block's draw size, normalized [0;1]; also the rotation pivot. */
    Vector2 RelativeOrigin;
    /** @brief true if @c CachedRenderSize holds a valid measurement to reuse for the origin. */
    bool HasCachedRenderSize;
    /** @brief Cached draw size in normalized-fitted units (from TextComponentRenderer_MeasureComponent). */
    Vector2 CachedRenderSize;
    /** @brief true to clip drawing to @c ScissorRelative (used e.g. to cut label text to its bounds). */
    bool HasScissor;
    /** @brief Clip rectangle in screen-relative [0;1] coordinates (x, y, width, height); used when @c HasScissor.
     *         The GPU scissor does not nest, so this replaces (then disables) any outer clip for this draw. */
    Rectangle ScissorRelative;
} ComponentRenderArguments;

/**
 * @brief A reusable text-component measuring/drawing device.
 *
 * Holds scratch buffers that are reused across calls to avoid per-frame allocation. Underscore-prefixed
 * fields are internal. Construct with TextComponentRenderer_Construct and release with
 * TextComponentRenderer_Deconstruct. Not thread-safe (the scratch buffers are shared across a call).
 */
typedef struct TextComponentRendererStruct
{
    /** @brief Reusable buffer of laid-out runs (internal element type). */
    GenericBuffer _runs;
    /** @brief Reusable byte buffer for building NUL-terminated text segments. */
    GenericBuffer _scratch;
} TextComponentRenderer;


// Helpers.
/**
 * @brief Builds default render arguments: multiplier 1, white tint, no rotation, top-left origin, no cache.
 * @param position Where to place the component's top-left origin.
 * @returns A ready-to-use ComponentRenderArguments.
 */
static inline ComponentRenderArguments ComponentRenderArguments_Create(RenderVector2D position)
{
    return (ComponentRenderArguments)
    {
        .Position = position,
        .AdditionalSizeMultiplier = 1.0f,
        .Tint = RenderColor_White(),
        .RotationRad = 0.0f,
        .RelativeOrigin = (Vector2){ .x = 0.0f, .y = 0.0f },
        .HasCachedRenderSize = false,
        .CachedRenderSize = (Vector2){ .x = 0.0f, .y = 0.0f },
        .HasScissor = false,
        .ScissorRelative = (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f }
    };
}


// Lifecycle.
/**
 * @brief Initializes a renderer with empty reusable scratch buffers.
 * @param self The renderer to initialize; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error TextComponentRenderer_Construct(TextComponentRenderer* self);

/**
 * @brief Releases the renderer's reusable buffers.
 * @param self The renderer to deconstruct, or NULL.
 * @returns Success (including the NULL case).
 */
Error TextComponentRenderer_Deconstruct(TextComponentRenderer* self);


// Measuring and drawing.
/**
 * @brief Measures a component's draw size, including its subcomponents and newlines.
 *
 * Returns the size in normalized-fitted units (see the file comment). Shadows do not contribute. The size
 * is measured at the component's own sizes (no additional multiplier); a caller that draws with an
 * additional multiplier scales the result by that multiplier. Suitable for caching in
 * ComponentRenderArguments::CachedRenderSize.
 * @param self The renderer; must not be NULL. Uses (and overwrites) its scratch buffers.
 * @param component The component to measure; must not be NULL.
 * @param outSize [out] Receives the measured size; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p component or @p outSize is NULL;
 *          ErrorCode_BufferTooLarge if the internal run list could not grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentRenderer_MeasureComponent(TextComponentRenderer* self, const TextComponent* component, Vector2* outSize);

/**
 * @brief Draws a component (and its subtree) through the render context using the given arguments.
 *
 * Lays the component out and draws each string run (with optional shadow, underline and strikethrough)
 * and sprite run, applying the additional size multiplier, tint, origin and rotation. Does not begin or
 * end a render pass, and does not touch the render target or shaders.
 * @param self The renderer; must not be NULL. Uses (and overwrites) its scratch buffers.
 * @param context The render context to draw through; must not be NULL.
 * @param component The component to draw; must not be NULL.
 * @param args The draw parameters; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if any pointer argument is NULL; ErrorCode_BufferTooLarge
 *          if the internal run list could not grow.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error TextComponentRenderer_Render(TextComponentRenderer* self,
    RenderContext* context,
    const TextComponent* component,
    const ComponentRenderArguments* args);
