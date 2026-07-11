#pragma once
#include <stdbool.h>
// The render wrapper is built on the renderer (RenderContext, RenderColor, RenderFloat) and draws raylib
// resources (Texture2D, Font, Model, Vector2, Rectangle), so both includes are unavoidable here.
#include "raylib/raylib.h"
#include "Renderer.h"
#include "wr/WRError.h"


/**
 * @file UIRenderContext.h
 * @brief The per-widget, widget-local drawing device handed to a widget's Render method.
 *
 * When the screen renders a widget it builds a UIRenderContext describing that widget's box in
 * screen-normalized [0;1] coordinates (its absolute position and size, composed down the widget tree)
 * plus the tint accumulated from its ancestors. The widget then draws using WIDGET-LOCAL [0;1]
 * coordinates — (0,0) is the widget's top-left, (1,1) its bottom-right — and the render context maps
 * those onto the screen and multiplies every draw's color by the accumulated tint. A per-draw color is
 * still accepted and multiplies with the tint, and because each child's context multiplies its own tint
 * onto the parent's, tint composes for the whole subtree.
 *
 * Clipping (AreSubWidgetsCut) is applied by the screen around a subtree via the GPU scissor, not by this
 * wrapper. Font sizes and line/border thicknesses are expressed in the underlying render context's
 * coordinate spaces (screen/buffer relative or pixels), NOT widget-local, since a constant on-screen
 * thickness or text size is almost always what is wanted; positions and box sizes are widget-local.
 *
 * A UIRenderContext is a light value type owning nothing (the underlying RenderContext is borrowed). It
 * is created cheaply on the stack per widget per frame via UIRenderContext_Create.
 */


// Types.
/**
 * @brief The full set of parameters for drawing a texture in widget-local space.
 *
 * Position and Size are widget-local [0;1]; the color multiplies with the context's accumulated tint.
 */
typedef struct UITextureDrawInfoStruct
{
    /** @brief The texture to draw; borrowed for the duration of the call. */
    Texture2D Texture;
    /** @brief Placement of the texture's origin, widget-local [0;1]. */
    Vector2 Position;
    /** @brief Size of the drawn rectangle, widget-local [0;1]. */
    Vector2 Size;
    /** @brief Normalized [0;1] sub-rectangle of the texture to draw. */
    Rectangle RelativeSourceRectangle;
    /** @brief Origin within the drawn rectangle, normalized [0;1]; also the rotation pivot. */
    Vector2 RelativeOrigin;
    /** @brief Rotation around the origin, in radians. */
    float RotationRad;
    /** @brief Draw color/tint, multiplied with the context's accumulated tint. */
    RenderColor Color;
} UITextureDrawInfo;

/**
 * @brief The full set of parameters for drawing text in widget-local space.
 *
 * Position is widget-local [0;1]; Size is a font height in the render context's spaces (screen/pixel),
 * not widget-local. The color multiplies with the context's accumulated tint. CachedDrawSize/HasCachedDrawSize
 * mirror the renderer's text caching (see TextRenderArguments).
 */
typedef struct UITextDrawInfoStruct
{
    /** @brief The text to draw as NUL-terminated UTF-8; borrowed for the duration of the call. */
    const unsigned char* Text;
    /** @brief The font to render with. */
    Font TargetFont;
    /** @brief Placement of the text's origin, widget-local [0;1]. */
    Vector2 Position;
    /** @brief Font size in the render context's spaces (a pixel/relative/fitted scalar), not widget-local. */
    RenderFloat Size;
    /** @brief Spacing between characters, as a multiplier of the size. */
    float SizeRelativeSpacing;
    /** @brief Origin within the whole text block, normalized [0;1]; also the rotation pivot. */
    Vector2 RelativeOrigin;
    /** @brief Rotation around the origin, in radians. */
    float RotationRad;
    /** @brief Draw color/tint, multiplied with the context's accumulated tint. */
    RenderColor Color;
    /** @brief Cached normalized draw size (font size 1.0); valid only when HasCachedDrawSize is true. */
    Vector2 CachedDrawSize;
    /** @brief true if CachedDrawSize holds a valid cached measurement to reuse. */
    bool HasCachedDrawSize;
} UITextDrawInfo;

/**
 * @brief A widget-local drawing device: an underlying render context plus a screen box and a tint.
 *
 * Underscore-prefixed fields are read-only outside this module; use the getters and the LocalToScreen /
 * LocalSizeToScreen helpers. Construct with UIRenderContext_Create.
 */
typedef struct UIRenderContextStruct
{
    /** @brief The underlying drawing device; borrowed, not owned. */
    RenderContext* _renderContext;
    /** @brief The widget's top-left in screen-normalized [0;1] coordinates. */
    Vector2 _absolutePosition;
    /** @brief The widget's size in screen-normalized [0;1] coordinates. */
    Vector2 _absoluteSize;
    /** @brief The tint accumulated from this widget and its ancestors, multiplied into every draw. */
    RenderColor _tint;
} UIRenderContext;


// Functions.
/**
 * @brief Multiplies two render colors (tint channels, brightness and opacity multiply componentwise).
 *
 * Used to compose a child's tint onto its parent's and to combine a per-draw color with the accumulated
 * tint. Channels are multiplied in the normalized [0;255] / [0;1] senses.
 * @param a The first color.
 * @param b The second color.
 * @returns The componentwise product.
 */
static inline RenderColor UIRenderColor_Multiply(RenderColor a, RenderColor b)
{
    RenderColor Result;
    Result.Tint.r = (unsigned char)(((int)a.Tint.r * (int)b.Tint.r) / 255);
    Result.Tint.g = (unsigned char)(((int)a.Tint.g * (int)b.Tint.g) / 255);
    Result.Tint.b = (unsigned char)(((int)a.Tint.b * (int)b.Tint.b) / 255);
    Result.Tint.a = (unsigned char)(((int)a.Tint.a * (int)b.Tint.a) / 255);
    Result.Brightness = a.Brightness * b.Brightness;
    Result.Opacity = a.Opacity * b.Opacity;
    return Result;
}

/**
 * @brief Initializes a widget-local render context for a widget's screen box and accumulated tint.
 * @param self The context to initialize; must not be NULL.
 * @param renderContext The underlying drawing device; borrowed, must outlive the context. Must not be NULL.
 * @param absolutePosition The widget's top-left in screen-normalized [0;1] coordinates.
 * @param absoluteSize The widget's size in screen-normalized [0;1] coordinates.
 * @param tint The tint accumulated from this widget and its ancestors.
 */
void UIRenderContext_Create(UIRenderContext* self,
    RenderContext* renderContext,
    Vector2 absolutePosition,
    Vector2 absoluteSize,
    RenderColor tint);

/**
 * @brief Returns the underlying render context (for advanced use / dropping to raw drawing).
 * @param self The context; must not be NULL.
 * @returns The borrowed underlying RenderContext.
 */
static inline RenderContext* UIRenderContext_GetRenderContext(const UIRenderContext* self)
{
    return self->_renderContext;
}

/**
 * @brief Returns the accumulated tint applied to every draw made through this context.
 * @param self The context; must not be NULL.
 * @returns The accumulated tint.
 */
static inline RenderColor UIRenderContext_GetTint(const UIRenderContext* self)
{
    return self->_tint;
}

/**
 * @brief Converts a widget-local [0;1] position to a screen-normalized [0;1] position.
 * @param self The context; must not be NULL.
 * @param local The widget-local position.
 * @returns The equivalent screen-normalized position.
 */
static inline Vector2 UIRenderContext_LocalToScreen(const UIRenderContext* self, Vector2 local)
{
    return (Vector2)
    {
        .x = self->_absolutePosition.x + (local.x * self->_absoluteSize.x),
        .y = self->_absolutePosition.y + (local.y * self->_absoluteSize.y)
    };
}

/**
 * @brief Converts a widget-local [0;1] size to a screen-normalized [0;1] size.
 * @param self The context; must not be NULL.
 * @param localSize The widget-local size.
 * @returns The equivalent screen-normalized size.
 */
static inline Vector2 UIRenderContext_LocalSizeToScreen(const UIRenderContext* self, Vector2 localSize)
{
    return (Vector2)
    {
        .x = localSize.x * self->_absoluteSize.x,
        .y = localSize.y * self->_absoluteSize.y
    };
}

/**
 * @brief Draws a filled rectangle in widget-local coordinates.
 * @param self The context; must not be NULL.
 * @param position Placement of the rectangle's origin, widget-local [0;1].
 * @param size Rectangle size, widget-local [0;1].
 * @param relativeOrigin Origin within the rectangle, normalized [0;1]; also the rotation pivot.
 * @param rotationRad Rotation around the origin, in radians.
 * @param color Draw color, multiplied with the accumulated tint.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error UIRenderContext_RenderRectangle(UIRenderContext* self,
    Vector2 position,
    Vector2 size,
    Vector2 relativeOrigin,
    float rotationRad,
    RenderColor color);

/**
 * @brief Draws an axis-aligned rectangle outline in widget-local coordinates.
 * @param self The context; must not be NULL.
 * @param position Top-left of the rectangle, widget-local [0;1].
 * @param size Rectangle size, widget-local [0;1].
 * @param thickness Border thickness in the render context's spaces (pixel type gives a constant width).
 * @param color Draw color, multiplied with the accumulated tint.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error UIRenderContext_RenderRectangleOutline(UIRenderContext* self,
    Vector2 position,
    Vector2 size,
    RenderFloat thickness,
    RenderColor color);

/**
 * @brief Draws a line segment in widget-local coordinates.
 * @param self The context; must not be NULL.
 * @param start Start point, widget-local [0;1].
 * @param end End point, widget-local [0;1].
 * @param thickness Line thickness in the render context's spaces (pixel type gives a constant width).
 * @param color Draw color, multiplied with the accumulated tint.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error UIRenderContext_RenderLine(UIRenderContext* self,
    Vector2 start,
    Vector2 end,
    RenderFloat thickness,
    RenderColor color);

/**
 * @brief Draws a texture in widget-local coordinates.
 * @param self The context; must not be NULL.
 * @param info The widget-local texture draw parameters; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p info is NULL.
 */
Error UIRenderContext_RenderTexture(UIRenderContext* self, const UITextureDrawInfo* info);

/**
 * @brief Draws text in widget-local coordinates.
 * @param self The context; must not be NULL.
 * @param info The widget-local text draw parameters; must not be NULL, and @c info->Text must be valid UTF-8.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p info is NULL.
 */
Error UIRenderContext_RenderText(UIRenderContext* self, const UITextDrawInfo* info);

/**
 * @brief Draws a 3D model, multiplying its color by the accumulated tint.
 *
 * The model is drawn in WORLD space (the widget-local transform does not apply to 3D geometry); this is
 * a convenience passthrough so a widget can render 3D content inside a 3D pass with the UI tint applied.
 * Must be called within a 3D pass opened on the underlying render context.
 * @param self The context; must not be NULL.
 * @param args The model draw parameters; must not be NULL. Its color is multiplied by the accumulated tint.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p args is NULL.
 */
Error UIRenderContext_RenderModel(UIRenderContext* self, const ModelRenderArguments* args);
