#include "UIBoxStyle.h"
#include "GameTexture.h"
#include "UIRenderContext.h"
#include "wr/WRMath.h"


// Macros.
/** The plain box's colours: a look that says nobody chose one. */
#define FALLBACK_FILL ((Color){ 64, 64, 64, 255 })
#define FALLBACK_OUTLINE ((Color){ 160, 160, 160, 255 })


// Fields.
static const unsigned char* const StateNames[UIElementState_Count] =
{
    u8"normal", u8"hovered", u8"pressed", u8"focused", u8"disabled", u8"selected", u8"invalid"
};


// Static functions.
static bool IsPositiveFinite(float value)
{
    return (value > 0.0f) && !Math_IsInfinityFloat(value);
}

static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}

/* The screen-pixel thickness of an outline: at least one pixel, whole, so it stays crisp at any scale. */
static float GetOutlinePixels(float thickness, float scale)
{
    float Pixels = RoundToPixel(thickness * scale);
    return (Pixels >= 1.0f) ? Pixels : 1.0f;
}

static float FiniteOrZero(float value)
{
    return (!Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value)) ? value : 0.0f;
}

/* One axis of a nine-slice: the three spans' lengths on the screen and in the art. Margins that do not fit the source are
 * cut to what fits, and corners larger than the box shrink by one factor so their ratio stays. */
static void GetNineSliceAxis(float boxStart, float boxLength, float sourceStart, float sourceLength, float marginStart,
    float marginEnd, float pixelsPerTexel, float* outEdges, float* outSourceEdges)
{
    float TexelsStart = Math_ClampFloat(marginStart, 0.0f, sourceLength);
    float TexelsEnd = Math_ClampFloat(marginEnd, 0.0f, sourceLength - TexelsStart);
    float PixelsStart = TexelsStart * pixelsPerTexel;
    float PixelsEnd = TexelsEnd * pixelsPerTexel;
    float Corners = PixelsStart + PixelsEnd;
    if (Corners > boxLength)
    {
        float Shrink = (Corners > 0.0f) ? (boxLength / Corners) : 0.0f;
        PixelsStart *= Shrink;
        PixelsEnd *= Shrink;
    }
    // Every edge whole, the inner ones derived from the outer: neighbours read the same number, so nothing gaps.
    float End = boxStart + boxLength;
    float InnerStart = Math_ClampFloat(RoundToPixel(boxStart + PixelsStart), boxStart, End);
    float InnerEnd = Math_ClampFloat(RoundToPixel(End - PixelsEnd), InnerStart, End);
    outEdges[0] = boxStart;
    outEdges[1] = InnerStart;
    outEdges[2] = InnerEnd;
    outEdges[3] = End;
    outSourceEdges[0] = sourceStart;
    outSourceEdges[1] = sourceStart + TexelsStart;
    outSourceEdges[2] = (sourceStart + sourceLength) - TexelsEnd;
    outSourceEdges[3] = sourceStart + sourceLength;
}

/* Draws a nine-slice's quads, each through the widget's context; empty ones are skipped. */
static void DrawNineSlice(const UIBoxStyle* style, UIRenderContext* context, Rectangle box, const UIBoxPaint* paint, float scale)
{
    float PixelScale = IsPositiveFinite(style->PixelScale) ? style->PixelScale : 1.0f;
    UINineSliceQuad Quads[UI_NINE_SLICE_QUAD_COUNT];
    UINineSlice_GetQuads(box, paint->Source, style->Margins, PixelScale * scale, Quads);
    Texture2D Art = GameTexture_GetRaylibTexture(style->Texture);
    for (size_t Index = 0U; Index < UI_NINE_SLICE_QUAD_COUNT; Index++)
    {
        if ((Quads[Index].Destination.width <= 0.0f) || (Quads[Index].Destination.height <= 0.0f))
        {
            continue;
        }
        UIRenderContext_RenderPixelTexture(context, Art, Quads[Index].Source, Quads[Index].Destination, paint->Fill);
    }
}


// Public functions.
Color UIStateColors_Resolve(const UIStateColors* colors, UIElementState state, Color fallback)
{
    if (((int32_t)state >= 0) && ((int32_t)state < (int32_t)UIElementState_Count) && colors->HasColor[state])
    {
        return colors->Colors[state];
    }
    return colors->HasColor[UIElementState_Normal] ? colors->Colors[UIElementState_Normal] : fallback;
}

UIBoxPaint UIBoxStyle_Resolve(const UIBoxStyle* style, UIElementState state)
{
    bool IsStateValid = ((int32_t)state >= 0) && ((int32_t)state < (int32_t)UIElementState_Count);
    UIElementState SourceState = (IsStateValid && style->HasSource[state]) ? state : UIElementState_Normal;
    return (UIBoxPaint)
    {
        .Fill = UIStateColors_Resolve(&style->Colors, state, WHITE),
        .Outline = UIStateColors_Resolve(&style->OutlineColors, state, WHITE),
        .Source = style->Sources[SourceState]
    };
}

const unsigned char* UIElementState_GetName(UIElementState state)
{
    bool IsStateValid = ((int32_t)state >= 0) && ((int32_t)state < (int32_t)UIElementState_Count);
    return IsStateValid ? StateNames[state] : NULL;
}

void UINineSlice_GetQuads(Rectangle box, Rectangle source, UIInsets margins, float pixelsPerTexel, UINineSliceQuad* outQuads)
{
    float PixelsPerTexel = IsPositiveFinite(pixelsPerTexel) ? pixelsPerTexel : 1.0f;
    float BoxWidth = IsPositiveFinite(box.width) ? box.width : 0.0f;
    float BoxHeight = IsPositiveFinite(box.height) ? box.height : 0.0f;
    float SourceWidth = IsPositiveFinite(source.width) ? source.width : 0.0f;
    float SourceHeight = IsPositiveFinite(source.height) ? source.height : 0.0f;

    float X[4];
    float Y[4];
    float SourceX[4];
    float SourceY[4];
    GetNineSliceAxis(FiniteOrZero(box.x), BoxWidth, FiniteOrZero(source.x), SourceWidth, margins.Left, margins.Right, PixelsPerTexel, X, SourceX);
    GetNineSliceAxis(FiniteOrZero(box.y), BoxHeight, FiniteOrZero(source.y), SourceHeight, margins.Top, margins.Bottom, PixelsPerTexel, Y, SourceY);
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        for (size_t Column = 0U; Column < 3U; Column++)
        {
            outQuads[(Row * 3U) + Column] = (UINineSliceQuad)
            {
                .Destination = { .x = X[Column], .y = Y[Row], .width = X[Column + 1U] - X[Column],
                    .height = Y[Row + 1U] - Y[Row] },
                .Source = { .x = SourceX[Column], .y = SourceY[Row], .width = SourceX[Column + 1U] - SourceX[Column],
                    .height = SourceY[Row + 1U] - SourceY[Row] }
            };
        }
    }
}

Error UIBoxStyle_Draw(const UIBoxStyle* style, UIRenderContext* context, Rectangle box, const UIBoxPaint* paint, float scale)
{
    if ((context == NULL) || (paint == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIBoxStyle_Draw: context and paint must not be NULL.");
    }
    if (style == NULL)
    {
        return UIBoxStyle_DrawFallback(context, box, scale);
    }
    switch (style->Kind)
    {
        case UIBoxKind_None:
            return Error_CreateSuccess();
        case UIBoxKind_Flat:
            UIRenderContext_RenderPixelRectangle(context, box, paint->Fill);
            if (style->HasOutline)
            {
                UIRenderContext_RenderPixelOutline(context, box, GetOutlinePixels(style->OutlineThickness, scale), paint->Outline);
            }
            return Error_CreateSuccess();
        case UIBoxKind_NineSlice:
            if (style->Texture == NULL)
            {
                return UIBoxStyle_DrawFallback(context, box, scale);
            }
            DrawNineSlice(style, context, box, paint, scale);
            return Error_CreateSuccess();
    }
    return UIBoxStyle_DrawFallback(context, box, scale); // A corrupted kind.
}

Error UIBoxStyle_DrawFallback(UIRenderContext* context, Rectangle box, float scale)
{
    if (context == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIBoxStyle_DrawFallback: context must not be NULL.");
    }
    UIRenderContext_RenderPixelRectangle(context, box, FALLBACK_FILL);
    UIRenderContext_RenderPixelOutline(context, box, GetOutlinePixels(1.0f, scale), FALLBACK_OUTLINE);
    return Error_CreateSuccess();
}
