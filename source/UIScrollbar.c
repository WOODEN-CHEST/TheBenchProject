#include "UIScrollbar.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Static functions: helpers.
static bool IsAxisValid(UIScrollAxis axis)
{
    return ((int32_t)axis >= 0) && ((int32_t)axis < (int32_t)UIScrollAxis_Count);
}

static float GetAxis(Vector2 vector, UIScrollAxis axis)
{
    return (axis == UIScrollAxis_Horizontal) ? vector.x : vector.y;
}

static Error ResolveScrollbar(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIScrollbarCapability** outScrollbar)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScrollbar: the widget is gone, or its screen has no kit.");
    }
    UIScrollbarCapability* Scrollbar = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Scrollbar));
    if (Scrollbar == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScrollbar: the widget is not a scroll bar.");
    }
    *outWidget = Target;
    *outScrollbar = Scrollbar;
    return Error_CreateSuccess();
}

/* The target's scrollable, or NULL when the bar is bound to nothing alive that has one. */
static UIScrollableCapability* GetTargetScrollable(const UIScrollbarCapability* self, const Widget* widget)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIKit* Kit = UIKit_FromScreen(Screen);
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(Screen, self->_targetId, &Target))
    {
        return NULL;
    }
    return Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Scrollable));
}

/* The end of the bar that is no track, in surface pixels. */
static float GetEndInsetPixels(const UIScrollbarCapability* self, const Widget* widget)
{
    return self->_endInset * Widget_GetEffectiveScale(widget);
}

/* The track along the bar's axis, as layout put the bar: its start and length in surface pixels (the box less the end inset). */
static void GetTrack(const UIScrollbarCapability* self, const Widget* widget, float* outStart, float* outLength)
{
    Rectangle Box = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    *outStart = (self->_axis == UIScrollAxis_Horizontal) ? Box.x : Box.y;
    float Length = (self->_axis == UIScrollAxis_Horizontal) ? Box.width : Box.height;
    *outLength = Math_MaxFloat(0.0f, Length - GetEndInsetPixels(self, widget));
}

/* Where along the track the pointer of a mouse input is, in surface pixels from the track's start. */
static float GetPointerAlong(const UIScrollbarCapability* self, const Widget* widget, Vector2 widgetPosition)
{
    Rectangle Box = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    float Length = (self->_axis == UIScrollAxis_Horizontal) ? Box.width : Box.height;
    return GetAxis(widgetPosition, self->_axis) * Length;
}

/* The thumb along the track, in surface pixels from the track's start. False with nothing to scroll. */
static bool GetThumbAlong(const UIScrollbarCapability* self, const Widget* widget, const UIScrollableCapability* scrollable, float trackLength,
    float* outStart, float* outLength)
{
    float MinThumb = self->_minThumbLength * Widget_GetEffectiveScale(widget);
    if (scrollable == NULL)
    {
        UIScrollbar_GetThumb(trackLength, MinThumb, 0.0f, 0.0f, 0.0f, outStart, outLength);
        return false;
    }
    UIScrollbar_GetThumb(trackLength, MinThumb, GetAxis(scrollable->_contentSize, self->_axis), GetAxis(scrollable->_viewSize, self->_axis),
        GetAxis(scrollable->_offset, self->_axis), outStart, outLength);
    return GetAxis(UIScrollableCapability_GetRange(scrollable), self->_axis) > 0.0f;
}

/* Moves the target along the bar's axis by a distance in the target's UI pixels. Returns how far it moved. */
static float ScrollTarget(const UIScrollbarCapability* self, UIScrollableCapability* scrollable, float distance)
{
    Vector2 Delta = (self->_axis == UIScrollAxis_Horizontal) ? (Vector2){ .x = distance, .y = 0.0f } : (Vector2){ .x = 0.0f, .y = distance };
    return GetAxis(UIScrollableCapability_ScrollBy(scrollable, Delta), self->_axis);
}

/* Pages once toward the pointer of the held press, unless the thumb has reached it. */
static void PageOnce(UIScrollbarCapability* self, const Widget* widget, UIScrollableCapability* scrollable)
{
    float TrackStart = 0.0f;
    float TrackLength = 0.0f;
    float ThumbStart = 0.0f;
    float ThumbLength = 0.0f;
    GetTrack(self, widget, &TrackStart, &TrackLength);
    if (!GetThumbAlong(self, widget, scrollable, TrackLength, &ThumbStart, &ThumbLength))
    {
        return;
    }
    bool IsBeyondThumb = (self->_pageDirection < 0.0f) ? (self->_pagePointerPixels < ThumbStart)
        : (self->_pagePointerPixels > (ThumbStart + ThumbLength));
    if (IsBeyondThumb)
    {
        ScrollTarget(self, scrollable, self->_pageDirection * GetAxis(scrollable->_viewSize, self->_axis));
    }
}

static void EndPress(UIScrollbarCapability* self)
{
    self->_isDragging = false;
    self->_isPaging = false;
}

/* A left press: on the thumb it holds the thumb where it was grabbed; on the track it pages toward the pointer at once. */
static void BeginPress(UIScrollbarCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    UIScrollableCapability* Scrollable = GetTargetScrollable(self, widget);
    float TrackStart = 0.0f;
    float TrackLength = 0.0f;
    float ThumbStart = 0.0f;
    float ThumbLength = 0.0f;
    GetTrack(self, widget, &TrackStart, &TrackLength);
    if (!GetThumbAlong(self, widget, Scrollable, TrackLength, &ThumbStart, &ThumbLength))
    {
        return; // Nothing to scroll: the press does nothing.
    }
    float Pointer = GetPointerAlong(self, widget, args->WidgetPosition);
    if ((Pointer >= ThumbStart) && (Pointer <= (ThumbStart + ThumbLength)))
    {
        self->_isDragging = true;
        self->_grabOffsetPixels = Pointer - ThumbStart;
        return;
    }
    self->_isPaging = true;
    self->_pagePointerPixels = Pointer;
    self->_pageDirection = (Pointer < ThumbStart) ? -1.0f : 1.0f;
    self->_pageCountdownSeconds = UI_SCROLLBAR_PAGE_DELAY_SECONDS;
    PageOnce(self, widget, Scrollable);
}

/* A drag: the thumb's start follows the pointer, less where it was grabbed. */
static void DragTo(UIScrollbarCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    UIScrollableCapability* Scrollable = GetTargetScrollable(self, widget);
    float TrackStart = 0.0f;
    float TrackLength = 0.0f;
    float ThumbStart = 0.0f;
    float ThumbLength = 0.0f;
    GetTrack(self, widget, &TrackStart, &TrackLength);
    if (!GetThumbAlong(self, widget, Scrollable, TrackLength, &ThumbStart, &ThumbLength))
    {
        return;
    }
    float Pointer = GetPointerAlong(self, widget, args->WidgetPosition);
    float Range = GetAxis(UIScrollableCapability_GetRange(Scrollable), self->_axis);
    float Offset = UIScrollbar_GetOffsetAt(TrackLength, ThumbLength, Range, Pointer - self->_grabOffsetPixels);
    Vector2 Target = Scrollable->_offset;
    if (self->_axis == UIScrollAxis_Horizontal)
    {
        Target.x = Offset;
    }
    else
    {
        Target.y = Offset;
    }
    UIScrollableCapability_SetOffset(Scrollable, Target);
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static UIScrollbarCapability* GetCapability(void* widget)
{
    return &((UIScrollbarLookBase*)widget)->Scrollbar;
}

static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScrollbar: the arguments are required.");
    }
    return UIScrollbarCapability_Construct(GetCapability(widget), widget, args);
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    UIScrollbarCapability_Update(GetCapability(widget), widget, time);
    return Error_CreateSuccess();
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    return UIScrollbarCapability_OnMouseInput(GetCapability(widget), widget, args);
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    UIScrollbarCapability_OnPressLost(GetCapability(widget), button);
    return Error_CreateSuccess();
}

static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIScrollbarCapability_OnScrollInput(GetCapability(widget), widget, args, outIsHandled);
    return Error_CreateSuccess();
}


// Fields.
static const UIKindCapability ScrollbarCapabilities[] = { { .Capability = UIElementCapability_Scrollbar, .Resolve = ResolveCapability } };

static const UIKindBehaviour ScrollbarBehaviour =
{
    .Kind = UIElementKind_Scrollbar,
    .Capabilities = ScrollbarCapabilities,
    .CapabilityCount = sizeof(ScrollbarCapabilities) / sizeof(ScrollbarCapabilities[0]),
    .BaseSize = sizeof(UIScrollbarLookBase),
    .IsFocusable = false,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Update = Behaviour_Update,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnScrollInput = Behaviour_OnScrollInput
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIScrollbar_GetKindBehaviour(void)
{
    return &ScrollbarBehaviour;
}


// Public functions: calling code.
Error UIScrollbar_SetTarget(UIScreen* screen, uint64_t widgetId, uint64_t targetId)
{
    Widget* Target = NULL;
    UIScrollbarCapability* Scrollbar = NULL;
    Error Result = ResolveScrollbar(screen, widgetId, &Target, &Scrollbar);
    if (Result.Code == ErrorCode_Success)
    {
        Scrollbar->_targetId = targetId;
        EndPress(Scrollbar);
    }
    return Result;
}

Error UIScrollbar_GetTarget(UIScreen* screen, uint64_t widgetId, uint64_t* outTargetId)
{
    Widget* Target = NULL;
    UIScrollbarCapability* Scrollbar = NULL;
    Error Result = ResolveScrollbar(screen, widgetId, &Target, &Scrollbar);
    if (Result.Code == ErrorCode_Success)
    {
        *outTargetId = Scrollbar->_targetId;
    }
    return Result;
}


// Public functions: behaviour helpers.
Error UIScrollbarCapability_Construct(UIScrollbarCapability* self, Widget* widget, const UIScrollbarArgs* args)
{
    const UIScrollbarOptions* Options = args->Options;
    if (!IsAxisValid(Options->Axis))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIScrollbar: an unknown axis.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_targetId = Options->TargetId;
    self->_axis = Options->Axis;
    self->_minThumbLength = 0.0f;
    self->_endInset = 0.0f;
    self->_isDragging = false;
    self->_grabOffsetPixels = 0.0f;
    self->_isPaging = false;
    self->_pagePointerPixels = 0.0f;
    self->_pageDirection = 1.0f;
    self->_pageCountdownSeconds = 0.0;
    self->_isPointerOnThumb = false;
    return Error_CreateSuccess();
}

void UIScrollbarCapability_Update(UIScrollbarCapability* self, Widget* widget, ProgramTime time)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    const GameInput* Input = UIScreen_GetInput(Screen);
    // A press whose release cannot reach the bar any more (a widget type that does not answer the loss) still ends.
    if ((Input != NULL) && (self->_isDragging || self->_isPaging)
        && ((GameInput_GetRawKeyState(Input, GameKey_MouseLeft) & GAME_INPUT_KEY_STATE_DOWN) == 0U))
    {
        EndPress(self);
    }
    UIScrollableCapability* Scrollable = GetTargetScrollable(self, widget);
    if (self->_isPaging && (Scrollable != NULL))
    {
        self->_pageCountdownSeconds -= time.PassedTime;
        while (self->_pageCountdownSeconds <= 0.0)
        {
            PageOnce(self, widget, Scrollable);
            self->_pageCountdownSeconds += UI_SCROLLBAR_PAGE_REPEAT_SECONDS;
        }
    }
    self->_isPointerOnThumb = false;
    if (Widget_IsHovered(widget) && UIScreen_IsPointerPresent(Screen))
    {
        float TrackStart = 0.0f;
        float TrackLength = 0.0f;
        float ThumbStart = 0.0f;
        float ThumbLength = 0.0f;
        GetTrack(self, widget, &TrackStart, &TrackLength);
        Vector2 Local = UIScreen_ScreenToWidget(Screen, widget, UIScreen_GetPointerPosition(Screen));
        float Pointer = GetPointerAlong(self, widget, Local);
        self->_isPointerOnThumb = GetThumbAlong(self, widget, Scrollable, TrackLength, &ThumbStart, &ThumbLength) && (Pointer >= ThumbStart)
            && (Pointer <= (ThumbStart + ThumbLength));
    }
}

void UIScrollbarCapability_SetMinThumbLength(UIScrollbarCapability* self, float length)
{
    self->_minThumbLength = ((length > 0.0f) && !Math_IsInfinityFloat(length)) ? length : 0.0f;
}

void UIScrollbarCapability_SetEndInset(UIScrollbarCapability* self, float inset)
{
    self->_endInset = ((inset > 0.0f) && !Math_IsInfinityFloat(inset)) ? inset : 0.0f;
}

bool UIScrollbarCapability_GetThumbBox(const UIScrollbarCapability* self, const Widget* widget, Rectangle track, float insetPixels,
    Rectangle* outThumb)
{
    float Inset = (insetPixels > 0.0f) ? insetPixels : 0.0f;
    bool IsHorizontal = self->_axis == UIScrollAxis_Horizontal;
    float TrackLength = Math_MaxFloat(0.0f, (IsHorizontal ? track.width : track.height) - GetEndInsetPixels(self, widget));
    float ThumbStart = 0.0f;
    float ThumbLength = 0.0f;
    bool HasScroll = GetThumbAlong(self, widget, GetTargetScrollable(self, widget), TrackLength, &ThumbStart, &ThumbLength);
    float Across = IsHorizontal ? track.height : track.width;
    float AcrossInset = Math_MinFloat(Inset, Across * 0.5f);
    float Start = Math_FloorFloat(ThumbStart + 0.5f);
    float End = Math_FloorFloat(ThumbStart + ThumbLength + 0.5f);
    if (IsHorizontal)
    {
        *outThumb = (Rectangle){ .x = track.x + Start, .y = track.y + AcrossInset, .width = End - Start, .height = Across - (2.0f * AcrossInset) };
    }
    else
    {
        *outThumb = (Rectangle){ .x = track.x + AcrossInset, .y = track.y + Start, .width = Across - (2.0f * AcrossInset), .height = End - Start };
    }
    return HasScroll;
}

UIElementState UIScrollbarCapability_GetThumbState(const UIScrollbarCapability* self)
{
    if (self->_isDragging)
    {
        return UIElementState_Pressed;
    }
    return self->_isPointerOnThumb ? UIElementState_Hovered : UIElementState_Normal;
}

Error UIScrollbarCapability_OnMouseInput(UIScrollbarCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    Error Result = UIElement_PostMouseCues(widget, args, true);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElement_PostMouseEvents(widget, args);
    }
    if ((Result.Code != ErrorCode_Success) || (args->Button != GameKey_MouseLeft))
    {
        return Result;
    }
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            if (!self->_isDragging && !self->_isPaging)
            {
                BeginPress(self, widget, args);
            }
            return Error_CreateSuccess();
        case UIMouseInputType_Move:
            if (self->_isDragging)
            {
                DragTo(self, widget, args);
            }
            else if (self->_isPaging)
            {
                self->_pagePointerPixels = GetPointerAlong(self, widget, args->WidgetPosition);
            }
            return Error_CreateSuccess();
        case UIMouseInputType_ButtonRelease:
            EndPress(self);
            return Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

void UIScrollbarCapability_OnPressLost(UIScrollbarCapability* self, GameKey button)
{
    if (button == GameKey_MouseLeft)
    {
        EndPress(self);
    }
}

void UIScrollbarCapability_OnScrollInput(UIScrollbarCapability* self, Widget* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIScrollableCapability* Scrollable = GetTargetScrollable(self, widget);
    if (Scrollable == NULL)
    {
        return;
    }
    // Along a horizontal bar a plain wheel scrolls across too.
    float Notches = ((self->_axis == UIScrollAxis_Horizontal) && (args->ScrollDelta.x != 0.0f)) ? args->ScrollDelta.x : args->ScrollDelta.y;
    float Moved = ScrollTarget(self, Scrollable, -Notches * Scrollable->_wheelStep);
    *outIsHandled = Moved != 0.0f;
}


// Public functions: the thumb's arithmetic.
void UIScrollbar_GetThumb(float trackLength, float minThumbLength, float contentLength, float viewLength, float offset, float* outStart,
    float* outLength)
{
    *outStart = 0.0f;
    *outLength = (trackLength > 0.0f) ? trackLength : 0.0f;
    if (!(trackLength > 0.0f) || !(contentLength > viewLength) || !(viewLength >= 0.0f))
    {
        return;
    }
    float Minimum = Math_MinFloat(Math_MaxFloat(minThumbLength, 0.0f), trackLength);
    float Length = Math_ClampFloat(trackLength * (viewLength / contentLength), Minimum, trackLength);
    float Range = contentLength - viewLength;
    float Share = Math_ClampFloat(offset / Range, 0.0f, 1.0f);
    *outLength = Length;
    *outStart = (trackLength - Length) * (Math_IsNaNFloat(Share) ? 0.0f : Share);
}

float UIScrollbar_GetOffsetAt(float trackLength, float thumbLength, float range, float thumbStart)
{
    float Travel = trackLength - thumbLength;
    if (!(Travel > 0.0f) || !(range > 0.0f) || Math_IsNaNFloat(thumbStart))
    {
        return 0.0f;
    }
    return Math_ClampFloat((thumbStart / Travel) * range, 0.0f, range);
}
