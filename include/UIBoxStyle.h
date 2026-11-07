#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Color, Rectangle and Vector2 are part of a box style; GameTexture is held borrowed, UIRenderContext draws it.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "UIPlacement.h"


/**
 * @file UIBoxStyle.h
 * @brief How a theme draws a box: nothing, a flat colour or nine-slice art, each with a colour per state.
 *
 * The one drawing routine every element's look goes through. A box style is plain data a theme holds. A white
 * nine-part button is a nine_slice style with colours multiplied in: its corners keep their size, its sides and its centre
 * stretch, so one piece of art fits every size, and a colour per state tints it.
 *
 * Resolving a state and cutting a box into nine quads are pure functions that need no GL (UIBoxStyle_Resolve,
 * UINineSlice_GetQuads); UIBoxStyle_Draw draws through the UIRenderContext, so it respects clips and tints.
 */


// Forward declarations.
/** @brief A texture asset; full type in GameTexture.h. Borrowed from the asset manager. */
typedef struct GameTextureStruct GameTexture;
/** @brief The widget-local drawing device; full type in UIRenderContext.h. */
typedef struct UIRenderContextStruct UIRenderContext;


// Macros.
/** @brief How many quads a nine-slice has: four corners, four sides and the centre. */
#define UI_NINE_SLICE_QUAD_COUNT ((size_t)9)


// Types.
/**
 * @brief An element's state, which picks its colours and its art. A state a theme leaves out falls back to normal; entered falls
 *        back to focused first.
 */
typedef enum UIElementStateEnum
{
    /** @brief Nothing special. */
    UIElementState_Normal,
    /** @brief The pointer is over it. */
    UIElementState_Hovered,
    /** @brief It is being pressed. */
    UIElementState_Pressed,
    /** @brief It has the keyboard's focus. */
    UIElementState_Focused,
    /** @brief It takes no input. */
    UIElementState_Disabled,
    /** @brief It is the chosen one (a toggled button, a selected row). */
    UIElementState_Selected,
    /** @brief Its value is refused. */
    UIElementState_Invalid,
    /** @brief It is being edited: an edit element that was entered, so its keys are its own. Left out, it shows focused. */
    UIElementState_Entered,
    /** @brief The number of states. */
    UIElementState_Count
} UIElementState;

/**
 * @brief A colour per state, with an explicit flag for the ones given. A plain value.
 */
typedef struct UIStateColorsStruct
{
    /** @brief The colours, by UIElementState; meaningful where HasColor is set. */
    Color Colors[UIElementState_Count];
    /** @brief Which states have a colour of their own. */
    bool HasColor[UIElementState_Count];
} UIStateColors;

/**
 * @brief What a box style draws.
 */
typedef enum UIBoxKindEnum
{
    /** @brief Nothing. */
    UIBoxKind_None,
    /** @brief A colour, optionally with an outline. */
    UIBoxKind_Flat,
    /** @brief A texture cut into nine parts, a colour multiplied in. */
    UIBoxKind_NineSlice
} UIBoxKind;

/**
 * @brief How to draw one box. Plain data with no owned memory; the texture is borrowed from the theme's dependencies.
 */
typedef struct UIBoxStyleStruct
{
    /** @brief What it draws. */
    UIBoxKind Kind;
    /** @brief The fill's colour for a flat box, the colour multiplied into the art for a nine-slice. */
    UIStateColors Colors;

    /** @brief A flat box's outline, inside its bounds: whether it has one. */
    bool HasOutline;
    /** @brief The outline's thickness in UI pixels; > 0 when HasOutline. */
    float OutlineThickness;
    /** @brief The outline's colour per state. */
    UIStateColors OutlineColors;

    /** @brief A nine-slice's texture, NULL when it did not load (the box then draws plain). Borrowed. */
    const GameTexture* Texture;
    /** @brief Index of the texture's name in the theme's table; meaningful for a nine-slice. */
    uint32_t TextureIndex;
    /** @brief The corners' sizes in texels. */
    UIInsets Margins;
    /** @brief Surface pixels per texel of the art, before the UI's scale: 2 draws small art at double size. */
    float PixelScale;
    /** @brief The art's rectangle in texels, per state; meaningful where HasSource is set. */
    Rectangle Sources[UIElementState_Count];
    /** @brief Which states have a rectangle of their own; normal's is the whole texture when the theme gives none. */
    bool HasSource[UIElementState_Count];
} UIBoxStyle;

/**
 * @brief What a box style comes to for one moment: its colours and its art's rectangle. Filled by UIBoxStyle_Resolve, or
 *        blended over a state change by the element.
 */
typedef struct UIBoxPaintStruct
{
    /** @brief The fill's colour, or what is multiplied into the art. */
    Color Fill;
    /** @brief The outline's colour. */
    Color Outline;
    /** @brief The art's rectangle in texels. */
    Rectangle Source;
} UIBoxPaint;

/**
 * @brief One of a nine-slice's quads: where it lands and which texels it shows.
 */
typedef struct UINineSliceQuadStruct
{
    /** @brief The destination in surface pixels; whole pixels, possibly of zero size. */
    Rectangle Destination;
    /** @brief The source in texels. */
    Rectangle Source;
} UINineSliceQuad;


// Functions.
/**
 * @brief Returns the colour of a state: its own, else (for entered) focused's, else normal's, else @p fallback.
 * @param colors The colours; must not be NULL.
 * @param state The state; a value out of range counts as normal.
 * @param fallback The colour when not even normal has one.
 * @returns The colour.
 */
Color UIStateColors_Resolve(const UIStateColors* colors, UIElementState state, Color fallback);

/**
 * @brief Returns what a box style comes to in a state, with the fallbacks of UIStateColors_Resolve (entered to focused, then
 *        normal), for the art's rectangle too.
 * @param style The style; must not be NULL.
 * @param state The state.
 * @returns The colours (white where none is given) and the art's rectangle.
 */
UIBoxPaint UIBoxStyle_Resolve(const UIBoxStyle* style, UIElementState state);

/**
 * @brief Returns a state's name in a theme's format ("normal", "hovered", ...).
 * @param state The state.
 * @returns The static name, or NULL for a value out of range.
 */
const unsigned char* UIElementState_GetName(UIElementState state);

/**
 * @brief Cuts a box into a nine-slice's quads.
 *
 * Corners keep their size, sides stretch along their length and the centre both ways. Quads share their edges exactly,
 * every edge a whole surface pixel, so there are no gaps or overlaps at any size and scale. Margins larger than the box
 * shrink evenly (both sides of an axis by the same factor), down to a box smaller than its corners; sources stay whole.
 * @param box The box in surface pixels; a size that is not a finite number > 0 gives empty quads.
 * @param source The art's rectangle in texels.
 * @param margins The corners' sizes in texels; values that do not fit the source count as what fits.
 * @param pixelsPerTexel Surface pixels per texel of the corners; a value that is not a finite number > 0 counts as 1.
 * @param outQuads [out] Receives UI_NINE_SLICE_QUAD_COUNT quads, row by row from the top-left. Must not be NULL.
 */
void UINineSlice_GetQuads(Rectangle box, Rectangle source, UIInsets margins, float pixelsPerTexel, UINineSliceQuad* outQuads);

/**
 * @brief Draws a box through a widget's render context, which keeps clips and tints.
 *
 * A NULL style, and a nine-slice whose texture did not load, draw the plain fallback box (UIBoxStyle_DrawFallback): never
 * a crash and never an error.
 * @param style The style, or NULL.
 * @param context The widget's render context; must not be NULL.
 * @param box The box in surface pixels.
 * @param paint What the style comes to now; must not be NULL.
 * @param scale Surface pixels per UI pixel (the screen's scale), for an outline and for the corners.
 * @returns Success; ErrorCode_IllegalArgument if @p context or @p paint is NULL.
 */
Error UIBoxStyle_Draw(const UIBoxStyle* style, UIRenderContext* context, Rectangle box, const UIBoxPaint* paint, float scale);

/**
 * @brief Draws the plain box of an element without a look: a grey fill and a lighter outline, one UI pixel thick.
 * @param context The widget's render context; must not be NULL.
 * @param box The box in surface pixels.
 * @param scale Surface pixels per UI pixel.
 * @returns Success; ErrorCode_IllegalArgument if @p context is NULL.
 */
Error UIBoxStyle_DrawFallback(UIRenderContext* context, Rectangle box, float scale);
