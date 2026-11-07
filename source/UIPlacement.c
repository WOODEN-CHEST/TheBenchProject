#include "UIPlacement.h"
#include "raylib/raymath.h"
#include "wr/WRMath.h"


// Macros.
/** How far below a half a sum may lie and still count as that half: at a thousand pixels the floats in it are this rough. */
#define PIXEL_EDGE_TOLERANCE (1.0 / 256.0)


// Static functions.
static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

static bool IsOffsetValid(Vector2 value)
{
    return (Math_AbsFloat(value.x) <= UI_PLACEMENT_MAX_OFFSET) && (Math_AbsFloat(value.y) <= UI_PLACEMENT_MAX_OFFSET);
}

static bool IsAnchorValid(Vector2 anchor)
{
    return (anchor.x >= 0.0f) && (anchor.x <= 1.0f) && (anchor.y >= 0.0f) && (anchor.y <= 1.0f);
}

/* Rounds half up, the same way on both sides of zero, so a shared edge stays shared wherever it lies. Noise in the sum does
 * not decide it: both edges of a box a whole number of pixels long round the same way, and the box keeps its length. */
static double RoundToPixel(double value)
{
    return Math_FloorDouble(value + 0.5 + PIXEL_EDGE_TOLERANCE);
}

/* Resolves one axis: the edges in surface pixels (double, so a far edge keeps its fraction), then back to the parent's
 * normalized space. */
static void ResolveAxis(float parentPosition, float parentSize, float surfacePixels, float scale, float anchorMin,
    float anchorMax, float offsetMin, float offsetMax, float* outPosition, float* outSize)
{
    double ParentLeft = (double)parentPosition * (double)surfacePixels;
    double ParentLength = (double)parentSize * (double)surfacePixels;
    bool IsUsable = IsFloatFinite(parentPosition) && IsFloatFinite(parentSize) && IsFloatFinite(surfacePixels)
        && IsFloatFinite(scale) && (scale > 0.0f) && (ParentLength > 0.0);
    if (!IsUsable)
    {
        *outPosition = 0.0f;
        *outSize = 0.0f;
        return;
    }
    double Start = RoundToPixel(ParentLeft + ((double)anchorMin * ParentLength) + ((double)offsetMin * (double)scale));
    double End = RoundToPixel(ParentLeft + ((double)anchorMax * ParentLength) + ((double)offsetMax * (double)scale));
    if (End < Start)
    {
        End = Start;
    }
    *outPosition = (float)((Start - ParentLeft) / ParentLength);
    *outSize = (float)((End - Start) / ParentLength);
}


// Public functions.
bool UIPlacement_IsValid(const UIPlacement* placement)
{
    return IsAnchorValid(placement->AnchorMin) && IsAnchorValid(placement->AnchorMax)
        && IsOffsetValid(placement->OffsetMin) && IsOffsetValid(placement->OffsetMax);
}

bool UIStackHint_IsValid(const UIStackHint* hint)
{
    bool IsModeValid = false;
    switch (hint->Mode)
    {
        case UIStackSizeMode_Content:
        case UIStackSizeMode_Fixed:
        case UIStackSizeMode_Fill:
            IsModeValid = true;
            break;
    }
    bool IsAlignValid = false;
    switch (hint->Align)
    {
        case UIStackAlign_Start:
        case UIStackAlign_Center:
        case UIStackAlign_End:
        case UIStackAlign_Stretch:
            IsAlignValid = true;
            break;
    }
    return IsModeValid && IsAlignValid && UILayoutLength_IsValid(hint->Size) && UILayoutLength_IsValid(hint->Weight)
        && (hint->Weight > 0.0f) && UILayoutLength_IsValid(hint->CrossSize);
}

bool UILayoutLength_IsValid(float length)
{
    return (length >= 0.0f) && (length <= UI_LAYOUT_MAX_LENGTH);
}

void UIPlacement_Resolve(const UIPlacement* placement, Vector2 parentPosition, Vector2 parentSize, Vector2 surfaceSizePixels,
    float scale, Vector2* outPosition, Vector2* outSize)
{
    ResolveAxis(parentPosition.x, parentSize.x, surfaceSizePixels.x, scale, placement->AnchorMin.x, placement->AnchorMax.x,
        placement->OffsetMin.x, placement->OffsetMax.x, &outPosition->x, &outSize->x);
    ResolveAxis(parentPosition.y, parentSize.y, surfaceSizePixels.y, scale, placement->AnchorMin.y, placement->AnchorMax.y,
        placement->OffsetMin.y, placement->OffsetMax.y, &outPosition->y, &outSize->y);
}


// Public functions: the render transform.
bool UIRenderTransform_IsValid(const UIRenderTransform* transform)
{
    bool IsScaleValid = (Math_AbsFloat(transform->VisualScale.x) <= UI_RENDER_TRANSFORM_MAX_SCALE)
        && (Math_AbsFloat(transform->VisualScale.y) <= UI_RENDER_TRANSFORM_MAX_SCALE);
    return IsFloatFinite(transform->Rotation) && IsFloatFinite(transform->Pivot.x) && IsFloatFinite(transform->Pivot.y)
        && IsOffsetValid(transform->Offset) && IsScaleValid;
}

bool UIRenderTransform_IsIdentity(const UIRenderTransform* transform)
{
    return (transform->Rotation == 0.0f) && (transform->Offset.x == 0.0f) && (transform->Offset.y == 0.0f)
        && (transform->VisualScale.x == 1.0f) && (transform->VisualScale.y == 1.0f);
}

/* The pivot in surface pixels. */
static Vector2 GetPivotPixels(const UIRenderTransform* transform, Rectangle boxPixels)
{
    return (Vector2)
    {
        .x = boxPixels.x + (transform->Pivot.x * boxPixels.width),
        .y = boxPixels.y + (transform->Pivot.y * boxPixels.height)
    };
}

Matrix UIRenderTransform_GetMatrix(const UIRenderTransform* transform, Rectangle boxPixels, float offsetScale)
{
    Vector2 Pivot = GetPivotPixels(transform, boxPixels);
    Matrix ToPivot = MatrixTranslate(-Pivot.x, -Pivot.y, 0.0f);
    Matrix Scale = MatrixScale(transform->VisualScale.x, transform->VisualScale.y, 1.0f);
    Matrix Turn = MatrixRotateZ(transform->Rotation);
    Matrix Back = MatrixTranslate(Pivot.x + (transform->Offset.x * offsetScale), Pivot.y + (transform->Offset.y * offsetScale), 0.0f);
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(ToPivot, Scale), Turn), Back);
}

Vector2 UIRenderTransform_ApplyToPoint(const UIRenderTransform* transform, Rectangle boxPixels, float offsetScale, Vector2 point)
{
    Vector2 Pivot = GetPivotPixels(transform, boxPixels);
    float Cos = Math_CosFloat(transform->Rotation);
    float Sin = Math_SinFloat(transform->Rotation);
    float X = (point.x - Pivot.x) * transform->VisualScale.x;
    float Y = (point.y - Pivot.y) * transform->VisualScale.y;
    return (Vector2)
    {
        .x = Pivot.x + (transform->Offset.x * offsetScale) + ((X * Cos) - (Y * Sin)),
        .y = Pivot.y + (transform->Offset.y * offsetScale) + ((X * Sin) + (Y * Cos))
    };
}

bool UIRenderTransform_TryInvertPoint(const UIRenderTransform* transform, Rectangle boxPixels, float offsetScale, Vector2 point,
    Vector2* outPoint)
{
    if ((transform->VisualScale.x == 0.0f) || (transform->VisualScale.y == 0.0f))
    {
        return false;
    }
    Vector2 Pivot = GetPivotPixels(transform, boxPixels);
    float Cos = Math_CosFloat(transform->Rotation);
    float Sin = Math_SinFloat(transform->Rotation);
    float X = point.x - Pivot.x - (transform->Offset.x * offsetScale);
    float Y = point.y - Pivot.y - (transform->Offset.y * offsetScale);
    // Turned back (the transpose of a turn is its inverse), then scaled back.
    float TurnedX = (X * Cos) + (Y * Sin);
    float TurnedY = (Y * Cos) - (X * Sin);
    *outPoint = (Vector2)
    {
        .x = Pivot.x + (TurnedX / transform->VisualScale.x),
        .y = Pivot.y + (TurnedY / transform->VisualScale.y)
    };
    return true;
}
