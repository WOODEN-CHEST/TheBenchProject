#include "UIScrollArea.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UIScrollbar.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Macros.
/** How many times a layout decides the bars again: each bar that shows narrows the view, which can call for the other. */
#define MAX_BAR_PASSES 3
/** How far, in UI pixels, the content may pass the view and still fit: float noise does not show a bar. */
#define FIT_TOLERANCE 0.01f


// Static functions: helpers.
static UIScrollAreaCapability* GetCapability(void* widget)
{
    return &((UIScrollAreaLookBase*)widget)->Area;
}

static bool IsBar(const UIScrollAreaCapability* self, const Widget* child)
{
    uint64_t Id = Widget_GetId(child);
    return (Id == self->_barIds[UIScrollAxis_Horizontal]) || (Id == self->_barIds[UIScrollAxis_Vertical]);
}

static UIScrollMode GetMode(const UIScrollAreaCapability* self, UIScrollAxis axis)
{
    return self->Scrollable._modes[axis];
}

/* The content's preferred size: the largest of its children's (every child but the bars, those that show). */
static Error MeasureContent(const UIScrollAreaCapability* self, Widget* widget, const UIMeasureContext* sizing, float availableWidth,
    Vector2* outSize)
{
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    for (size_t Index = 0; Index < Widget_GetSubWidgetCount(widget); Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if (IsBar(self, Child) || !Widget_IsRendered(Child))
        {
            continue;
        }
        Vector2 Size = { .x = 0.0f, .y = 0.0f };
        Error Result = Widget_Measure(Child, sizing, availableWidth, &Size);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        outSize->x = Math_MaxFloat(outSize->x, Size.x);
        outSize->y = Math_MaxFloat(outSize->y, Size.y);
    }
    return Error_CreateSuccess();
}

/* How thick a bar is across, in the area's UI pixels: what its look measures. 0 for a bar that is gone. */
static Error MeasureBarThickness(const UIScrollAreaCapability* self, Widget* widget, UIScrollAxis axis, const UIMeasureContext* sizing,
    float* outThickness)
{
    *outThickness = 0.0f;
    Widget* Bar = NULL;
    if (!UIScreen_TryGetWidgetById(Widget_GetScreen(widget), self->_barIds[axis], &Bar))
    {
        return Error_CreateSuccess();
    }
    Vector2 Size = { .x = 0.0f, .y = 0.0f };
    Error Result = Widget_Measure(Bar, sizing, UI_MEASURE_UNBOUNDED_WIDTH, &Size);
    *outThickness = (axis == UIScrollAxis_Vertical) ? Size.x : Size.y;
    return Result;
}

/* Places a bar along its edge, or with no size when it does not show. The vertical bar runs the whole height: where the horizontal
 * one meets it, the corner is the end of its track, which covers it (so the content never shows there) and its thumb never enters. */
static Error PlaceBar(const UIScrollAreaCapability* self, Widget* widget, UIScrollAxis axis, bool isShown, float thickness, float otherThickness)
{
    UIKit* Kit = UIKit_FromScreen(Widget_GetScreen(widget));
    Widget* Bar = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(Widget_GetScreen(widget), self->_barIds[axis], &Bar))
    {
        return Error_CreateSuccess();
    }
    UIPlacement Placement = { .AnchorMin = { .x = 1.0f, .y = 1.0f }, .AnchorMax = { .x = 1.0f, .y = 1.0f }, .OffsetMin = { .x = 0.0f, .y = 0.0f },
        .OffsetMax = { .x = 0.0f, .y = 0.0f } };
    if (isShown && (axis == UIScrollAxis_Vertical))
    {
        Placement = (UIPlacement){ .AnchorMin = { .x = 1.0f, .y = 0.0f }, .AnchorMax = { .x = 1.0f, .y = 1.0f },
            .OffsetMin = { .x = -thickness, .y = 0.0f }, .OffsetMax = { .x = 0.0f, .y = 0.0f } };
    }
    else if (isShown)
    {
        Placement = (UIPlacement){ .AnchorMin = { .x = 0.0f, .y = 1.0f }, .AnchorMax = { .x = 1.0f, .y = 1.0f },
            .OffsetMin = { .x = 0.0f, .y = -thickness }, .OffsetMax = { .x = -otherThickness, .y = 0.0f } };
    }
    UIScrollbarCapability* Scrollbar = Widget_GetCapability(Bar, UIKit_GetCapabilityId(Kit, UIElementCapability_Scrollbar));
    if (Scrollbar != NULL)
    {
        // The bar counts in its own UI pixels: the area's, less a scale of its own.
        float Inset = (axis == UIScrollAxis_Vertical) ? otherThickness : 0.0f;
        UIScrollbarCapability_SetEndInset(Scrollbar, Inset * (Widget_GetEffectiveScale(widget) / Widget_GetEffectiveScale(Bar)));
    }
    return Widget_SetPlacement(Bar, &Placement);
}

/* Decides which bars show and how large the content and the view are, starting from what the last layout decided: then an idle area
 * measures its content at the width it measured it at before, and the measure is cached. */
static Error DecideBars(UIScrollAreaCapability* self, Widget* widget, const UIMeasureContext* sizing, Vector2 boxSize, const float thickness[UIScrollAxis_Count],
    Vector2* outContent, Vector2* outView)
{
    bool IsShown[UIScrollAxis_Count] = { self->_isBarShown[UIScrollAxis_Horizontal], self->_isBarShown[UIScrollAxis_Vertical] };
    for (int32_t Axis = 0; Axis < (int32_t)UIScrollAxis_Count; Axis++)
    {
        IsShown[Axis] = (GetMode(self, (UIScrollAxis)Axis) == UIScrollMode_Always)
            || ((GetMode(self, (UIScrollAxis)Axis) == UIScrollMode_Auto) && IsShown[Axis]);
    }
    for (int32_t Pass = 0; Pass < MAX_BAR_PASSES; Pass++)
    {
        Vector2 View =
        {
            .x = Math_MaxFloat(0.0f, boxSize.x - (IsShown[UIScrollAxis_Vertical] ? thickness[UIScrollAxis_Vertical] : 0.0f)),
            .y = Math_MaxFloat(0.0f, boxSize.y - (IsShown[UIScrollAxis_Horizontal] ? thickness[UIScrollAxis_Horizontal] : 0.0f))
        };
        bool IsAcrossFixed = GetMode(self, UIScrollAxis_Horizontal) == UIScrollMode_Disabled;
        Vector2 Measured = { .x = 0.0f, .y = 0.0f };
        Error Result = MeasureContent(self, widget, sizing, IsAcrossFixed ? View.x : UI_MEASURE_UNBOUNDED_WIDTH, &Measured);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        *outView = View;
        *outContent = (Vector2)
        {
            .x = IsAcrossFixed ? View.x : Math_MaxFloat(Measured.x, View.x),
            .y = (GetMode(self, UIScrollAxis_Vertical) == UIScrollMode_Disabled) ? View.y : Math_MaxFloat(Measured.y, View.y)
        };
        bool Needs[UIScrollAxis_Count] =
        {
            (GetMode(self, UIScrollAxis_Horizontal) == UIScrollMode_Always)
                || ((GetMode(self, UIScrollAxis_Horizontal) == UIScrollMode_Auto) && (outContent->x > (View.x + FIT_TOLERANCE))),
            (GetMode(self, UIScrollAxis_Vertical) == UIScrollMode_Always)
                || ((GetMode(self, UIScrollAxis_Vertical) == UIScrollMode_Auto) && (outContent->y > (View.y + FIT_TOLERANCE)))
        };
        if ((Needs[UIScrollAxis_Horizontal] == IsShown[UIScrollAxis_Horizontal]) && (Needs[UIScrollAxis_Vertical] == IsShown[UIScrollAxis_Vertical]))
        {
            break;
        }
        IsShown[UIScrollAxis_Horizontal] = Needs[UIScrollAxis_Horizontal];
        IsShown[UIScrollAxis_Vertical] = Needs[UIScrollAxis_Vertical];
    }
    self->_isBarShown[UIScrollAxis_Horizontal] = IsShown[UIScrollAxis_Horizontal];
    self->_isBarShown[UIScrollAxis_Vertical] = IsShown[UIScrollAxis_Vertical];
    return Error_CreateSuccess();
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static void* ResolveScrollable(void* widget)
{
    return &GetCapability(widget)->Scrollable;
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScrollArea: the arguments are required.");
    }
    const UIScrollAreaArgs* Arguments = args;
    return UIScrollAreaCapability_Construct(GetCapability(widget), widget, Arguments->Options);
}

/* The content at its offset, cut to the box; the bars along the right and bottom edges by the modes. */
static Error Behaviour_Layout(void* widget, const UILayoutContext* context)
{
    UIScrollAreaCapability* Area = GetCapability(widget);
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    Vector2 BoxSize = { .x = Pixels.width / Scale, .y = Pixels.height / Scale };
    float Thickness[UIScrollAxis_Count] = { 0.0f, 0.0f };
    Error Result = MeasureBarThickness(Area, widget, UIScrollAxis_Horizontal, context->Sizing, &Thickness[UIScrollAxis_Horizontal]);
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasureBarThickness(Area, widget, UIScrollAxis_Vertical, context->Sizing, &Thickness[UIScrollAxis_Vertical]);
    }
    Vector2 Content = { .x = 0.0f, .y = 0.0f };
    Vector2 View = { .x = 0.0f, .y = 0.0f };
    if (Result.Code == ErrorCode_Success)
    {
        Result = DecideBars(Area, widget, context->Sizing, BoxSize, Thickness, &Content, &View);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIScrollableCapability_SetSizes(&Area->Scrollable, Content, View);
    Area->_barThickness[UIScrollAxis_Horizontal] = Thickness[UIScrollAxis_Horizontal];
    Area->_barThickness[UIScrollAxis_Vertical] = Thickness[UIScrollAxis_Vertical];

    Vector2 Offset = Area->Scrollable._offset;
    UIPlacement ContentPlacement = UIPlacement_FromTopLeft(-Offset.x, -Offset.y, Content.x, Content.y);
    for (size_t Index = 0; Index < Widget_GetSubWidgetCount(widget); Index++)
    {
        Widget* Child = Widget_GetSubWidgetAt(widget, Index);
        if (!IsBar(Area, Child))
        {
            Result = Widget_SetPlacement(Child, &ContentPlacement);
            if (Result.Code != ErrorCode_Success)
            {
                return Result;
            }
        }
    }
    Area->Scrollable._placedOffset = Offset;
    Area->Scrollable._viewWidgetId = Widget_GetId(widget);
    bool IsHorizontalShown = Area->_isBarShown[UIScrollAxis_Horizontal];
    bool IsVerticalShown = Area->_isBarShown[UIScrollAxis_Vertical];
    Result = PlaceBar(Area, widget, UIScrollAxis_Vertical, IsVerticalShown, Thickness[UIScrollAxis_Vertical],
        IsHorizontalShown ? Thickness[UIScrollAxis_Horizontal] : 0.0f);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return PlaceBar(Area, widget, UIScrollAxis_Horizontal, IsHorizontalShown, Thickness[UIScrollAxis_Horizontal],
        IsVerticalShown ? Thickness[UIScrollAxis_Vertical] : 0.0f);
}

/* The wheel scrolls it while it can still scroll that way, and goes on up the tree at the end. Shift turns a plain wheel across. */
static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIScrollAreaCapability* Area = GetCapability(widget);
    bool IsShifted = (args->Modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U;
    bool IsSideways = IsShifted || ((args->ScrollDelta.x != 0.0f) && (args->ScrollDelta.y == 0.0f));
    float Notches = IsSideways ? ((args->ScrollDelta.y != 0.0f) ? args->ScrollDelta.y : args->ScrollDelta.x) : args->ScrollDelta.y;
    UIScrollAxis Axis = IsSideways ? UIScrollAxis_Horizontal : UIScrollAxis_Vertical;
    float Distance = -Notches * Area->Scrollable._wheelStep;
    if (!UIScrollableCapability_CanScroll(&Area->Scrollable, Axis, Distance))
    {
        return Error_CreateSuccess();
    }
    Vector2 Delta = (Axis == UIScrollAxis_Horizontal) ? (Vector2){ .x = Distance, .y = 0.0f } : (Vector2){ .x = 0.0f, .y = Distance };
    UIScrollableCapability_ScrollBy(&Area->Scrollable, Delta);
    *outIsHandled = true;
    return Error_CreateSuccess();
}

/* Page Up and Page Down that the focused widget inside it left alone scroll it by a page, while it can. */
static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    bool IsDown = (args->Type == UIKeyboardInputType_KeyPress) || (args->Type == UIKeyboardInputType_KeyRepeat);
    if (!IsDown || ((args->Key != GameKey_PageUp) && (args->Key != GameKey_PageDown)))
    {
        return Error_CreateSuccess();
    }
    UIScrollAreaCapability* Area = GetCapability(widget);
    float Distance = Area->Scrollable._viewSize.y * ((args->Key == GameKey_PageUp) ? -1.0f : 1.0f);
    if (UIScrollableCapability_CanScroll(&Area->Scrollable, UIScrollAxis_Vertical, Distance))
    {
        UIScrollableCapability_ScrollBy(&Area->Scrollable, (Vector2){ .x = 0.0f, .y = Distance });
        *outIsHandled = true;
    }
    return Error_CreateSuccess();
}

/* Focus moving to a widget inside it by the keyboard or by code scrolls it into view; a press does not, so the content never jumps
 * under the pointer. */
static Error Behaviour_OnDescendantFocus(void* widget, uint64_t widgetId)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    if (UIScreen_HasHeldPress(Screen))
    {
        return Error_CreateSuccess();
    }
    return UIScrollable_RevealWidget(Screen, Widget_GetId(widget), widgetId);
}


// Fields.
static const UIKindCapability ScrollAreaCapabilities[] = { { .Capability = UIElementCapability_Scrollable, .Resolve = ResolveScrollable } };

static const UIKindBehaviour ScrollAreaBehaviour =
{
    .Kind = UIElementKind_ScrollArea,
    .Capabilities = ScrollAreaCapabilities,
    .CapabilityCount = sizeof(ScrollAreaCapabilities) / sizeof(ScrollAreaCapabilities[0]),
    .BaseSize = sizeof(UIScrollAreaLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Layout = Behaviour_Layout,
    .OnScrollInput = Behaviour_OnScrollInput,
    .OnKeyboardInput = Behaviour_OnKeyboardInput,
    .OnDescendantFocus = Behaviour_OnDescendantFocus
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIScrollArea_GetKindBehaviour(void)
{
    return &ScrollAreaBehaviour;
}


// Public functions.
Error UIScrollAreaCapability_Construct(UIScrollAreaCapability* self, Widget* widget, const UIScrollAreaOptions* options)
{
    if (!UIScrollMode_IsValid(options->Horizontal) || !UIScrollMode_IsValid(options->Vertical) || !(options->WheelStep > 0.0f)
        || !(options->WheelStep <= UI_SCROLLABLE_MAX_WHEEL_STEP))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIScrollArea: known modes and a wheel step within (0, UI_SCROLLABLE_MAX_WHEEL_STEP] are required.");
    }
    Error Result = UIElement_ApplyOptions(widget, &options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIScrollableCapability_Construct(&self->Scrollable, options->Horizontal, options->Vertical, options->WheelStep);
    for (int32_t Axis = 0; Axis < (int32_t)UIScrollAxis_Count; Axis++)
    {
        self->_barIds[Axis] = WIDGET_ID_INVALID;
        self->_isBarShown[Axis] = false;
        self->_barThickness[Axis] = 0.0f;
    }
    // Cut to its box: the content scrolls under its edges.
    return Widget_SetAreSubWidgetsCut(widget, true);
}

bool UIScrollArea_IsBarShown(const UIScrollAreaCapability* self, UIScrollAxis axis)
{
    return ((int32_t)axis >= 0) && ((int32_t)axis < (int32_t)UIScrollAxis_Count) && self->_isBarShown[axis];
}

float UIScrollArea_GetBarThickness(const UIScrollAreaCapability* self, UIScrollAxis axis)
{
    return (((int32_t)axis >= 0) && ((int32_t)axis < (int32_t)UIScrollAxis_Count)) ? self->_barThickness[axis] : 0.0f;
}

void UIScrollArea_SetBars(UIScrollAreaCapability* self, uint64_t horizontalBarId, uint64_t verticalBarId)
{
    self->_barIds[UIScrollAxis_Horizontal] = horizontalBarId;
    self->_barIds[UIScrollAxis_Vertical] = verticalBarId;
}

Error UIScrollArea_MeasurePreferred(UIScrollAreaCapability* self, Widget* widget, const UIMeasureContext* context, float availableWidth,
    Vector2* outSize)
{
    float Thickness[UIScrollAxis_Count] = { 0.0f, 0.0f };
    Error Result = MeasureBarThickness(self, widget, UIScrollAxis_Horizontal, context, &Thickness[UIScrollAxis_Horizontal]);
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasureBarThickness(self, widget, UIScrollAxis_Vertical, context, &Thickness[UIScrollAxis_Vertical]);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsVerticalAlways = GetMode(self, UIScrollAxis_Vertical) == UIScrollMode_Always;
    bool IsHorizontalAlways = GetMode(self, UIScrollAxis_Horizontal) == UIScrollMode_Always;
    float Available = availableWidth;
    if (IsVerticalAlways)
    {
        Available = Math_MaxFloat(0.0f, availableWidth - Thickness[UIScrollAxis_Vertical]);
    }
    Result = MeasureContent(self, widget, context, Available, outSize);
    outSize->x += IsVerticalAlways ? Thickness[UIScrollAxis_Vertical] : 0.0f;
    outSize->y += IsHorizontalAlways ? Thickness[UIScrollAxis_Horizontal] : 0.0f;
    return Result;
}
