#include "UIScrollable.h"
#include "UIKit.h"
#include "UIPlacement.h"
#include "UIScreen.h"
#include "UIWidget.h"
#include "wr/WRMath.h"


// Static functions.
static bool IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

/* A size the piece keeps: a finite number within [0, UI_LAYOUT_MAX_LENGTH], else 0 (or the most). */
static float SanitizeLength(float length)
{
    if (!(length > 0.0f))
    {
        return 0.0f; // Also a NaN.
    }
    return (length < UI_LAYOUT_MAX_LENGTH) ? length : UI_LAYOUT_MAX_LENGTH;
}

static float GetAxis(Vector2 vector, UIScrollAxis axis)
{
    return (axis == UIScrollAxis_Horizontal) ? vector.x : vector.y;
}

static float ClampOffset(float offset, float range)
{
    return (offset > 0.0f) ? ((offset < range) ? offset : range) : 0.0f;
}

/* Clamps the offset into the range; true when it moved. */
static bool ClampToRange(UIScrollableCapability* self)
{
    Vector2 Range = UIScrollableCapability_GetRange(self);
    Vector2 Clamped = { .x = ClampOffset(self->_offset.x, Range.x), .y = ClampOffset(self->_offset.y, Range.y) };
    bool HasMoved = (Clamped.x != self->_offset.x) || (Clamped.y != self->_offset.y);
    self->_offset = Clamped;
    return HasMoved;
}

static Error ResolveScrollable(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIScrollableCapability** outScrollable)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScrollable: the widget is gone, or its screen has no kit.");
    }
    UIScrollableCapability* Scrollable = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Scrollable));
    if (Scrollable == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScrollable: the widget has no scrollable.");
    }
    *outWidget = Target;
    *outScrollable = Scrollable;
    return Error_CreateSuccess();
}

/* Where a widget's box is on the surface as layout put it, moved by what the scrollables between it and @p ancestor have scrolled since
 * their content was placed (each of their offsets as it is now). */
static Rectangle GetCurrentPixelBox(const UIScreen* screen, const Widget* widget, const Widget* ancestor, uint64_t scrollableCapability)
{
    Rectangle Box = UIScreen_GetWidgetPixelBox(screen, widget);
    for (Widget* Current = Widget_GetParent(widget); (Current != NULL) && (Current != ancestor); Current = Widget_GetParent(Current))
    {
        const UIScrollableCapability* Between = Widget_GetCapability(Current, scrollableCapability);
        if (Between == NULL)
        {
            continue;
        }
        float Scale = Widget_GetEffectiveScale(Current);
        Box.x += (Between->_placedOffset.x - Between->_offset.x) * Scale;
        Box.y += (Between->_placedOffset.y - Between->_offset.y) * Scale;
    }
    return Box;
}


// Public functions: calling code.
Error UIScrollable_SetOffset(UIScreen* screen, uint64_t widgetId, Vector2 offset)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsFloatFinite(offset.x) || !IsFloatFinite(offset.y))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScrollable_SetOffset: the offset must be finite.");
    }
    UIScrollableCapability_SetOffset(Scrollable, offset);
    return Error_CreateSuccess();
}

Error UIScrollable_GetOffset(UIScreen* screen, uint64_t widgetId, Vector2* outOffset)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code == ErrorCode_Success)
    {
        *outOffset = Scrollable->_offset;
    }
    return Result;
}

Error UIScrollable_ScrollBy(UIScreen* screen, uint64_t widgetId, Vector2 delta)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsFloatFinite(delta.x) || !IsFloatFinite(delta.y))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScrollable_ScrollBy: the distance must be finite.");
    }
    UIScrollableCapability_ScrollBy(Scrollable, delta);
    return Error_CreateSuccess();
}

Error UIScrollable_EnsureVisible(UIScreen* screen, uint64_t widgetId, Rectangle contentBox)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsValid = IsFloatFinite(contentBox.x) && IsFloatFinite(contentBox.y) && IsFloatFinite(contentBox.width)
        && IsFloatFinite(contentBox.height) && (contentBox.width >= 0.0f) && (contentBox.height >= 0.0f);
    if (!IsValid)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScrollable_EnsureVisible: the box must be finite, of a size >= 0.");
    }
    UIScrollableCapability_EnsureVisible(Scrollable, contentBox);
    return Error_CreateSuccess();
}

Error UIScrollable_RevealWidget(UIScreen* screen, uint64_t widgetId, uint64_t descendantId)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Widget* Descendant = NULL;
    Widget* View = NULL;
    bool IsInside = false;
    if (UIScreen_TryGetWidgetById(screen, descendantId, &Descendant))
    {
        for (const Widget* Current = Widget_GetParent(Descendant); (Current != NULL) && !IsInside; Current = Widget_GetParent(Current))
        {
            IsInside = Current == Target;
        }
    }
    if (!IsInside)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScrollable_RevealWidget: the widget is not alive inside the scrollable.");
    }
    if (!UIScreen_TryGetWidgetById(screen, Scrollable->_viewWidgetId, &View))
    {
        return Error_CreateSuccess(); // No view laid out yet: nothing to scroll by.
    }
    uint64_t Capability = UIKit_GetCapabilityId(UIKit_FromScreen(screen), UIElementCapability_Scrollable);
    float Scale = Widget_GetEffectiveScale(View);
    Rectangle Box = GetCurrentPixelBox(screen, Descendant, Target, Capability);
    Rectangle ViewBox = UIScreen_GetWidgetPixelBox(screen, View);
    Rectangle ContentBox =
    {
        .x = ((Box.x - ViewBox.x) / Scale) + Scrollable->_placedOffset.x,
        .y = ((Box.y - ViewBox.y) / Scale) + Scrollable->_placedOffset.y,
        .width = Box.width / Scale,
        .height = Box.height / Scale
    };
    UIScrollableCapability_EnsureVisible(Scrollable, ContentBox);
    return Error_CreateSuccess();
}

Error UIScrollable_GetRange(UIScreen* screen, uint64_t widgetId, Vector2* outRange)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code == ErrorCode_Success)
    {
        *outRange = UIScrollableCapability_GetRange(Scrollable);
    }
    return Result;
}

Error UIScrollable_GetSizes(UIScreen* screen, uint64_t widgetId, Vector2* outContentSize, Vector2* outViewSize)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outContentSize != NULL)
    {
        *outContentSize = Scrollable->_contentSize;
    }
    if (outViewSize != NULL)
    {
        *outViewSize = Scrollable->_viewSize;
    }
    return Error_CreateSuccess();
}

Error UIScrollable_SetModes(UIScreen* screen, uint64_t widgetId, UIScrollMode horizontal, UIScrollMode vertical)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!UIScrollMode_IsValid(horizontal) || !UIScrollMode_IsValid(vertical))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScrollable_SetModes: an unknown mode.");
    }
    if ((Scrollable->_modes[UIScrollAxis_Horizontal] != horizontal) || (Scrollable->_modes[UIScrollAxis_Vertical] != vertical))
    {
        Scrollable->_modes[UIScrollAxis_Horizontal] = horizontal;
        Scrollable->_modes[UIScrollAxis_Vertical] = vertical;
        ClampToRange(Scrollable);
        Widget_InvalidateMeasure(Target);
    }
    return Error_CreateSuccess();
}

Error UIScrollable_SetWheelStep(UIScreen* screen, uint64_t widgetId, float step)
{
    Widget* Target = NULL;
    UIScrollableCapability* Scrollable = NULL;
    Error Result = ResolveScrollable(screen, widgetId, &Target, &Scrollable);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!(step > 0.0f) || !(step <= UI_SCROLLABLE_MAX_WHEEL_STEP))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScrollable_SetWheelStep: the step must be within (0, UI_SCROLLABLE_MAX_WHEEL_STEP].");
    }
    Scrollable->_wheelStep = step;
    return Error_CreateSuccess();
}


// Public functions: the capability.
void UIScrollableCapability_Construct(UIScrollableCapability* self, UIScrollMode horizontal, UIScrollMode vertical, float wheelStep)
{
    self->_offset = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_contentSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_viewSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_modes[UIScrollAxis_Horizontal] = UIScrollMode_IsValid(horizontal) ? horizontal : UIScrollMode_Disabled;
    self->_modes[UIScrollAxis_Vertical] = UIScrollMode_IsValid(vertical) ? vertical : UIScrollMode_Disabled;
    self->_wheelStep = ((wheelStep > 0.0f) && (wheelStep <= UI_SCROLLABLE_MAX_WHEEL_STEP)) ? wheelStep : UI_SCROLLABLE_DEFAULT_WHEEL_STEP;
    self->_placedOffset = (Vector2){ .x = 0.0f, .y = 0.0f };
    self->_viewWidgetId = WIDGET_ID_INVALID;
}

bool UIScrollMode_IsValid(UIScrollMode mode)
{
    return ((int32_t)mode >= (int32_t)UIScrollMode_Disabled) && ((int32_t)mode <= (int32_t)UIScrollMode_Hidden);
}

Vector2 UIScrollableCapability_GetRange(const UIScrollableCapability* self)
{
    Vector2 Range = { .x = 0.0f, .y = 0.0f };
    if (self->_modes[UIScrollAxis_Horizontal] != UIScrollMode_Disabled)
    {
        Range.x = Math_MaxFloat(0.0f, self->_contentSize.x - self->_viewSize.x);
    }
    if (self->_modes[UIScrollAxis_Vertical] != UIScrollMode_Disabled)
    {
        Range.y = Math_MaxFloat(0.0f, self->_contentSize.y - self->_viewSize.y);
    }
    return Range;
}

bool UIScrollableCapability_SetSizes(UIScrollableCapability* self, Vector2 contentSize, Vector2 viewSize)
{
    self->_contentSize = (Vector2){ .x = SanitizeLength(contentSize.x), .y = SanitizeLength(contentSize.y) };
    self->_viewSize = (Vector2){ .x = SanitizeLength(viewSize.x), .y = SanitizeLength(viewSize.y) };
    return ClampToRange(self);
}

bool UIScrollableCapability_SetOffset(UIScrollableCapability* self, Vector2 offset)
{
    Vector2 Old = self->_offset;
    if (IsFloatFinite(offset.x))
    {
        self->_offset.x = offset.x;
    }
    if (IsFloatFinite(offset.y))
    {
        self->_offset.y = offset.y;
    }
    ClampToRange(self);
    return (self->_offset.x != Old.x) || (self->_offset.y != Old.y);
}

Vector2 UIScrollableCapability_ScrollBy(UIScrollableCapability* self, Vector2 delta)
{
    Vector2 Old = self->_offset;
    Vector2 Target = Old;
    if (IsFloatFinite(delta.x))
    {
        Target.x += delta.x;
    }
    if (IsFloatFinite(delta.y))
    {
        Target.y += delta.y;
    }
    UIScrollableCapability_SetOffset(self, Target);
    return (Vector2){ .x = self->_offset.x - Old.x, .y = self->_offset.y - Old.y };
}

bool UIScrollableCapability_EnsureVisible(UIScrollableCapability* self, Rectangle contentBox)
{
    bool IsValid = IsFloatFinite(contentBox.x) && IsFloatFinite(contentBox.y) && IsFloatFinite(contentBox.width)
        && IsFloatFinite(contentBox.height) && (contentBox.width >= 0.0f) && (contentBox.height >= 0.0f);
    if (!IsValid)
    {
        return false;
    }
    Vector2 Target =
    {
        .x = UIScrollable_GetOffsetShowing(self->_offset.x, contentBox.x, contentBox.width, self->_viewSize.x),
        .y = UIScrollable_GetOffsetShowing(self->_offset.y, contentBox.y, contentBox.height, self->_viewSize.y)
    };
    return UIScrollableCapability_SetOffset(self, Target);
}

bool UIScrollableCapability_CanScroll(const UIScrollableCapability* self, UIScrollAxis axis, float direction)
{
    if (((int32_t)axis < 0) || ((int32_t)axis >= (int32_t)UIScrollAxis_Count))
    {
        return false;
    }
    float Range = GetAxis(UIScrollableCapability_GetRange(self), axis);
    float Offset = GetAxis(self->_offset, axis);
    if (direction < 0.0f)
    {
        return Offset > 0.0f;
    }
    return (direction > 0.0f) && (Offset < Range);
}

float UIScrollable_GetOffsetShowing(float offset, float spanStart, float spanLength, float viewLength)
{
    float SpanEnd = spanStart + spanLength;
    if (spanLength > viewLength)
    {
        bool IsViewInside = (offset >= spanStart) && ((offset + viewLength) <= SpanEnd);
        return IsViewInside ? offset : spanStart;
    }
    if (spanStart < offset)
    {
        return spanStart;
    }
    if (SpanEnd > (offset + viewLength))
    {
        return SpanEnd - viewLength;
    }
    return offset;
}
