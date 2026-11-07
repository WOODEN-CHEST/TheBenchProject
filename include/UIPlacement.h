#pragma once
#include <stdbool.h>
// Vector2 is part of every placement; raylib's plain struct, no drawing or input symbols are used.
#include "raylib/raylib.h"


/**
 * @file UIPlacement.h
 * @brief Where a widget sits, in UI pixels: placements, insets and a stack's hints, with their pure arithmetic.
 *
 * A UI PIXEL is a surface pixel times the screen's scale (UIScreen_SetScale; from its host's scale policy, a screen in the world
 * its own). A widget's geometry is normalized to its parent, which a dense panel, a row of a fixed height and a nine-slice
 * corner cannot use: they need sizes that do not change with the window. A PLACEMENT says where the widget's box sits in
 * its parent's: two anchors in [0;1] of the parent's box and two offsets from them in UI pixels. One structure covers a
 * fixed size at any corner (both anchors alike) and a stretch (anchors apart). The screen's layout pass turns it into the
 * normalized position and size, each edge rounded to a whole surface pixel, so edges stay crisp and two boxes sharing an
 * edge in UI pixels share it on the surface at any scale.
 *
 * A RENDER TRANSFORM (UIRenderTransform) is how a widget shows apart from where layout put it: a rotation, a visual offset and
 * a visual scale around a pivot. Drawing and pointing go through it; layout, measuring and keyboard navigation never see it.
 *
 * Everything here is pure: plain values, no widgets, no state.
 */


// Macros.
/** @brief The longest a stack's padding and spacing, a hint's sizes and weight and a preferred size can be, in UI pixels: what a
 *  stack adds up stays a number however many children it has. */
#define UI_LAYOUT_MAX_LENGTH 1e9f
/** @brief The largest offset of a placement from its anchor, in UI pixels: far beyond any surface, far below where a float ends. */
#define UI_PLACEMENT_MAX_OFFSET 1e30f
/** @brief The largest visual scale a render transform may give an axis, either way (a negative one mirrors). */
#define UI_RENDER_TRANSFORM_MAX_SCALE 1000.0f


// Types.
/**
 * @brief A widget's box in its parent's, in anchors and UI pixel offsets. A plain value; build one with the
 *        UIPlacement_* helpers.
 *
 * The box's left edge is at AnchorMin.x of the parent's width plus OffsetMin.x UI pixels, its right edge at AnchorMax.x
 * plus OffsetMax.x; the same on the other axis.
 */
typedef struct UIPlacementStruct
{
    /** @brief The anchor of the box's top-left corner, each component in [0;1] of the parent's box. */
    Vector2 AnchorMin;
    /** @brief The anchor of the box's bottom-right corner, each component in [0;1] of the parent's box. */
    Vector2 AnchorMax;
    /** @brief The top-left corner's offset from its anchor, in UI pixels; at most UI_PLACEMENT_MAX_OFFSET either way. */
    Vector2 OffsetMin;
    /** @brief The bottom-right corner's offset from its anchor, in UI pixels; at most UI_PLACEMENT_MAX_OFFSET either way. */
    Vector2 OffsetMax;
} UIPlacement;

/**
 * @brief Four distances from the sides of a box, in UI pixels (or texels, for a nine-slice's margins).
 */
typedef struct UIInsetsStruct
{
    /** @brief The distance from the left side. */
    float Left;
    /** @brief The distance from the top side. */
    float Top;
    /** @brief The distance from the right side. */
    float Right;
    /** @brief The distance from the bottom side. */
    float Bottom;
} UIInsets;

/**
 * @brief How a stack sizes a child along its main axis (a column's height, a row's width).
 */
typedef enum UIStackSizeModeEnum
{
    /** @brief As large as the child's preferred size (Widget_Measure). */
    UIStackSizeMode_Content,
    /** @brief Exactly UIStackHint.Size UI pixels. */
    UIStackSizeMode_Fixed,
    /** @brief A share of what the content and fixed children leave, by UIStackHint.Weight. */
    UIStackSizeMode_Fill
} UIStackSizeMode;

/**
 * @brief Where a stack puts a child across its main axis (a column's width, a row's height).
 */
typedef enum UIStackAlignEnum
{
    /** @brief The child's cross size, at the start (a column's left, a row's top). */
    UIStackAlign_Start,
    /** @brief The child's cross size, centred. */
    UIStackAlign_Center,
    /** @brief The child's cross size, at the end (a column's right, a row's bottom). */
    UIStackAlign_End,
    /** @brief The whole cross size of the stack's content box. */
    UIStackAlign_Stretch
} UIStackAlign;

/**
 * @brief How a child wants to sit in the stack that holds it. Kept on the child, set through the stack
 *        (StackWidget_SetChildHint) or the element's creation options. A plain value; build one with the UIStackHint_*
 *        helpers.
 */
typedef struct UIStackHintStruct
{
    /** @brief How its main size is found. */
    UIStackSizeMode Mode;
    /** @brief The main size in UI pixels, for UIStackSizeMode_Fixed; within [0, UI_LAYOUT_MAX_LENGTH]. */
    float Size;
    /** @brief The share of what is left, for UIStackSizeMode_Fill; above 0, at most UI_LAYOUT_MAX_LENGTH. */
    float Weight;
    /** @brief Where it sits across the main axis. */
    UIStackAlign Align;
    /** @brief Whether CrossSize replaces its preferred cross size (not used with UIStackAlign_Stretch). */
    bool HasCrossSize;
    /** @brief The cross size in UI pixels, when HasCrossSize; within [0, UI_LAYOUT_MAX_LENGTH]. */
    float CrossSize;
} UIStackHint;


/**
 * @brief How a widget shows, apart from where layout put it. A plain value; build one with UIRenderTransform_Create.
 *
 * Seen from its parent, a point of the widget's box goes to Pivot + Offset + Rotate(Rotation, VisualScale * (point - Pivot)): it
 * is scaled about the pivot, turned about it, then moved. Everything is worked out in surface pixels, where the axes have the
 * same unit (normalized coordinates are not, so a turn there would shear).
 */
typedef struct UIRenderTransformStruct
{
    /** @brief The turn in radians, clockwise on the surface (its y runs down); finite. */
    float Rotation;
    /** @brief The point it turns and scales about, in [0;1] of its box per axis ((0.5, 0.5) is its centre); finite, and may lie
     *  outside the box. */
    Vector2 Pivot;
    /** @brief How far it shows from where layout put it, in its parent's UI pixels (the surface's axes, not its own); each within
     *  UI_PLACEMENT_MAX_OFFSET. */
    Vector2 Offset;
    /** @brief How much larger it shows per axis; each within UI_RENDER_TRANSFORM_MAX_SCALE either way. 0 on an axis draws it as a
     *  line there and holds no point; a negative value mirrors it. Measuring and text layout never see it. */
    Vector2 VisualScale;
} UIRenderTransform;


// Helpers.
/**
 * @brief A box of a fixed size: its @p pivot point (a share of the box itself) sits @p offset UI pixels from @p anchor.
 * @param anchor Where in the parent's box, each component in [0;1].
 * @param pivot Which point of the box stands there: (0, 0) its top-left, (1, 1) its bottom-right.
 * @param offset The distance from the anchor, in UI pixels.
 * @param size The box's size, in UI pixels.
 * @returns The placement.
 */
static inline UIPlacement UIPlacement_Fixed(Vector2 anchor, Vector2 pivot, Vector2 offset, Vector2 size)
{
    Vector2 TopLeft = { .x = offset.x - (pivot.x * size.x), .y = offset.y - (pivot.y * size.y) };
    return (UIPlacement)
    {
        .AnchorMin = anchor,
        .AnchorMax = anchor,
        .OffsetMin = TopLeft,
        .OffsetMax = { .x = TopLeft.x + size.x, .y = TopLeft.y + size.y }
    };
}

/**
 * @brief A box at UI pixel coordinates from the parent's top-left corner.
 * @param x The left edge, in UI pixels.
 * @param y The top edge, in UI pixels.
 * @param width The width, in UI pixels.
 * @param height The height, in UI pixels.
 * @returns The placement.
 */
static inline UIPlacement UIPlacement_FromTopLeft(float x, float y, float width, float height)
{
    return (UIPlacement)
    {
        .AnchorMin = { .x = 0.0f, .y = 0.0f },
        .AnchorMax = { .x = 0.0f, .y = 0.0f },
        .OffsetMin = { .x = x, .y = y },
        .OffsetMax = { .x = x + width, .y = y + height }
    };
}

/**
 * @brief A box that fills its parent's, less @p insets.
 * @param insets The distances from the parent's sides, in UI pixels.
 * @returns The placement.
 */
static inline UIPlacement UIPlacement_Stretch(UIInsets insets)
{
    return (UIPlacement)
    {
        .AnchorMin = { .x = 0.0f, .y = 0.0f },
        .AnchorMax = { .x = 1.0f, .y = 1.0f },
        .OffsetMin = { .x = insets.Left, .y = insets.Top },
        .OffsetMax = { .x = -insets.Right, .y = -insets.Bottom }
    };
}

/**
 * @brief The same distance on all four sides.
 * @param value The distance.
 * @returns The insets.
 */
static inline UIInsets UIInsets_All(float value)
{
    return (UIInsets){ .Left = value, .Top = value, .Right = value, .Bottom = value };
}

/**
 * @brief A hint that sizes the child to its content, stretched across.
 * @returns The hint.
 */
static inline UIStackHint UIStackHint_Content(void)
{
    return (UIStackHint)
    {
        .Mode = UIStackSizeMode_Content,
        .Size = 0.0f,
        .Weight = 1.0f,
        .Align = UIStackAlign_Stretch,
        .HasCrossSize = false,
        .CrossSize = 0.0f
    };
}

/**
 * @brief A hint of a fixed main size, stretched across.
 * @param size The main size, in UI pixels.
 * @returns The hint.
 */
static inline UIStackHint UIStackHint_Fixed(float size)
{
    UIStackHint Hint = UIStackHint_Content();
    Hint.Mode = UIStackSizeMode_Fixed;
    Hint.Size = size;
    return Hint;
}

/**
 * @brief A hint that takes a share of what is left, stretched across.
 * @param weight The share's weight.
 * @returns The hint.
 */
static inline UIStackHint UIStackHint_Fill(float weight)
{
    UIStackHint Hint = UIStackHint_Content();
    Hint.Mode = UIStackSizeMode_Fill;
    Hint.Weight = weight;
    return Hint;
}


/**
 * @brief The render transform that changes nothing: no turn, the pivot at the centre, no offset, a visual scale of 1.
 * @returns The transform.
 */
static inline UIRenderTransform UIRenderTransform_Create(void)
{
    return (UIRenderTransform)
    {
        .Rotation = 0.0f,
        .Pivot = { .x = 0.5f, .y = 0.5f },
        .Offset = { .x = 0.0f, .y = 0.0f },
        .VisualScale = { .x = 1.0f, .y = 1.0f }
    };
}


// Functions.
/**
 * @brief Returns whether a placement can be used: its anchors in [0;1] and its offsets within UI_PLACEMENT_MAX_OFFSET.
 * @param placement The placement; must not be NULL.
 * @returns true if valid.
 */
bool UIPlacement_IsValid(const UIPlacement* placement);

/**
 * @brief Returns whether a stack hint can be used: a known mode and alignment, a size and a cross size that are valid
 *        lengths (UILayoutLength_IsValid), and a weight above 0 that is one too.
 * @param hint The hint; must not be NULL.
 * @returns true if valid.
 */
bool UIStackHint_IsValid(const UIStackHint* hint);

/**
 * @brief Returns whether a length can be used in a stack: within [0, UI_LAYOUT_MAX_LENGTH]; a NaN is not.
 * @param length The length in UI pixels.
 * @returns true if valid.
 */
bool UILayoutLength_IsValid(float length);

/**
 * @brief Turns a placement into a widget's parent-relative normalized position and size.
 *
 * Each edge is found in surface pixels and rounded to a whole one, float noise aside: a box a whole number of pixels long
 * keeps that length wherever it lies. A box whose far edge comes before its near edge has zero size at its near edge. A parent of zero size, or a surface or a scale that is not a positive finite number, gives a
 * position and a size of zero on that axis, never a NaN.
 * @param placement The placement; must not be NULL, and valid (UIPlacement_IsValid).
 * @param parentPosition The parent's top-left in the surface's normalized [0;1] space.
 * @param parentSize The parent's size in the surface's normalized [0;1] space.
 * @param surfaceSizePixels The surface's size in pixels.
 * @param scale Surface pixels per UI pixel.
 * @param outPosition [out] Receives the position, relative to the parent. Must not be NULL.
 * @param outSize [out] Receives the size, relative to the parent. Must not be NULL.
 */
void UIPlacement_Resolve(const UIPlacement* placement, Vector2 parentPosition, Vector2 parentSize, Vector2 surfaceSizePixels,
    float scale, Vector2* outPosition, Vector2* outSize);


// The render transform.
/**
 * @brief Returns whether a render transform's values are usable: every value finite, the offsets within UI_PLACEMENT_MAX_OFFSET
 *        and the visual scale within UI_RENDER_TRANSFORM_MAX_SCALE.
 * @param transform The transform; must not be NULL.
 * @returns true if valid.
 */
bool UIRenderTransform_IsValid(const UIRenderTransform* transform);

/**
 * @brief Returns whether a render transform changes nothing: no turn, no offset and a visual scale of 1 (the pivot does not
 *        matter then).
 * @param transform The transform; must not be NULL.
 * @returns true if it is the identity.
 */
bool UIRenderTransform_IsIdentity(const UIRenderTransform* transform);

/**
 * @brief Returns a render transform as a matrix in surface pixels, in raymath's convention (column vectors, the translation in
 *        m12 and m13), for a widget whose box is @p boxPixels.
 * @param transform The transform; must not be NULL.
 * @param boxPixels The widget's box as layout put it, in surface pixels.
 * @param offsetScale Surface pixels per UI pixel of the offset: its parent's effective scale.
 * @returns The matrix.
 */
Matrix UIRenderTransform_GetMatrix(const UIRenderTransform* transform, Rectangle boxPixels, float offsetScale);

/**
 * @brief Returns where a render transform shows a point of its widget: the point in surface pixels as layout sees it, moved as
 *        UIRenderTransform_GetMatrix moves it.
 * @param transform The transform; must not be NULL.
 * @param boxPixels The widget's box as layout put it, in surface pixels.
 * @param offsetScale Surface pixels per UI pixel of the offset.
 * @param point The point in surface pixels.
 * @returns Where it shows, in surface pixels.
 */
Vector2 UIRenderTransform_ApplyToPoint(const UIRenderTransform* transform, Rectangle boxPixels, float offsetScale, Vector2 point);

/**
 * @brief Finds the point of a widget a render transform shows at a given place: the inverse of UIRenderTransform_ApplyToPoint.
 * @param transform The transform; must not be NULL.
 * @param boxPixels The widget's box as layout put it, in surface pixels.
 * @param offsetScale Surface pixels per UI pixel of the offset.
 * @param point Where it shows, in surface pixels.
 * @param outPoint [out] Receives the point as layout sees it, in surface pixels, when there is one. Must not be NULL.
 * @returns true when there is one; false for a visual scale of 0 on an axis, where the transform holds no point.
 */
bool UIRenderTransform_TryInvertPoint(const UIRenderTransform* transform, Rectangle boxPixels, float offsetScale, Vector2 point,
    Vector2* outPoint);
