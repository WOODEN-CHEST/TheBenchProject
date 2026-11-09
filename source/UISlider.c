#include "UISlider.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Static functions: small helpers.
static bool IsDoubleFinite(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value);
}

static bool IsOrientationValid(UISliderOrientation orientation)
{
    return (orientation == UISliderOrientation_Horizontal) || (orientation == UISliderOrientation_Vertical);
}

static double ClampShare(double share)
{
    return (share > 0.0) ? ((share < 1.0) ? share : 1.0) : 0.0;
}

static bool HasModifier(uint8_t modifiers, uint8_t flag)
{
    return (modifiers & flag) != 0U;
}

static Error ResolveSlider(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UISliderCapability** outSlider)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UISlider: the widget is gone, or its screen has no kit.");
    }
    UISliderCapability* Slider = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Slider));
    if (Slider == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UISlider: the widget is not a slider.");
    }
    *outWidget = Target;
    *outSlider = Slider;
    return Error_CreateSuccess();
}

/* The track's ends: a drag's held track, else the drag range around the value (a value past a soft range sits at the end it passed). */
static void GetTrack(const UISliderCapability* self, double* outMinimum, double* outMaximum)
{
    if (self->_hasDragTrack)
    {
        *outMinimum = self->_dragTrackMinimum;
        *outMaximum = self->_dragTrackMaximum;
        return;
    }
    if (!UIValueSettings_GetDragRangeAround(UIValueCapability_GetSettings(&self->Value), UIValueCapability_GetValue(&self->Value), outMinimum,
        outMaximum))
    {
        *outMinimum = 0.0;
        *outMaximum = 0.0;
    }
}

/* Holds the track a drag runs on: the drag range around the value now. */
static void HoldDragTrack(UISliderCapability* self)
{
    self->_hasDragTrack = false;
    GetTrack(self, &self->_dragTrackMinimum, &self->_dragTrackMaximum);
    self->_hasDragTrack = true;
}

/* The value at a share of the track: through the conversion, snapped, the ends exact. */
static double GetValueAtShare(const UISliderCapability* self, double share)
{
    double Minimum = 0.0;
    double Maximum = 0.0;
    GetTrack(self, &Minimum, &Maximum);
    return UIValueSettings_FromShare(UIValueCapability_GetSettings(&self->Value), Minimum, Maximum, share);
}

static bool IsInSession(const UISliderCapability* self)
{
    return self->_isDragging || (self->_sessionKey != GameKey_Unknown);
}

/* The change in progress ends: committed unless the value ended where it began. */
static Error EndSession(UISliderCapability* self, Widget* widget)
{
    self->_isDragging = false;
    self->_hasDragTrack = false;
    self->_sessionKey = GameKey_Unknown;
    return UIValueCapability_EndChange(&self->Value, widget);
}

/* The change in progress is taken back; while its key stays down, what is left of it does nothing. */
static Error CancelSession(UISliderCapability* self, Widget* widget)
{
    self->_endedKey = self->_sessionKey;
    self->_isDragging = false;
    self->_hasDragTrack = false;
    self->_sessionKey = GameKey_Unknown;
    return UIValueCapability_CancelChange(&self->Value, widget);
}

/* The track's length and the thumb's along it, in surface pixels, as layout put the box (a render transform turns the drawing and the
 * pointer alike, so the drag's arithmetic stays along the box). */
static void GetTrackPixels(const UISliderCapability* self, Widget* widget, double* outLength, double* outThumb)
{
    Rectangle Box = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    *outLength = (double)((self->_orientation == UISliderOrientation_Vertical) ? Box.height : Box.width);
    *outThumb = (double)self->_thumbLength * (double)Widget_GetEffectiveScale(widget);
}

/* Where the pointer is along the track from its low end, in surface pixels: from the left, or from the bottom when upright. */
static double GetPointerAlong(const UISliderCapability* self, const WidgetMouseInputArgs* args, double length)
{
    double Along = (self->_orientation == UISliderOrientation_Vertical) ? (1.0 - (double)args->WidgetPosition.y) : (double)args->WidgetPosition.x;
    return Along * length;
}

/* The pointer's share of the track, not clamped. */
static double GetPointerShare(const UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    double Length = 0.0;
    double Thumb = 0.0;
    GetTrackPixels(self, widget, &Length, &Thumb);
    return UIValue_GetUnclampedPointerFraction(GetPointerAlong(self, args, Length), Length, Thumb);
}

/* A press: on the thumb it holds the thumb where it was grabbed and changes nothing, on the track it brings the thumb under the
 * pointer. */
static Error BeginDrag(UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    double Length = 0.0;
    double Thumb = 0.0;
    HoldDragTrack(self);
    GetTrackPixels(self, widget, &Length, &Thumb);
    double Pointer = GetPointerAlong(self, args, Length);
    double PointerShare = UIValue_GetUnclampedPointerFraction(Pointer, Length, Thumb);
    double ThumbShare = UISliderCapability_GetShare(self);
    double Travel = Length - Thumb;
    double Margin = (Travel > 0.0) ? ((Thumb * 0.5) / Travel) : 0.0;
    double FromCentre = Pointer - ((Thumb * 0.5) + (ThumbShare * ((Travel > 0.0) ? Travel : 0.0)));
    bool IsFine = HasModifier(args->Modifiers, GAME_KEY_MODIFIER_SHIFT);
    self->_isDragging = true;
    UIValueCapability_BeginChange(&self->Value);
    if (Math_AbsDouble(FromCentre) <= (Thumb * 0.5))
    {
        UISliderDrag_Begin(&self->_drag, ThumbShare, ThumbShare - PointerShare, PointerShare, Margin, IsFine);
        return Error_CreateSuccess();
    }
    UISliderDrag_Begin(&self->_drag, ClampShare(PointerShare), 0.0, PointerShare, Margin, IsFine);
    return UIValueCapability_MoveTo(&self->Value, widget, GetValueAtShare(self, self->_drag.Share), NULL);
}

static Error MoveDrag(UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    bool IsFine = HasModifier(args->Modifiers, GAME_KEY_MODIFIER_SHIFT);
    double Share = UISliderDrag_Move(&self->_drag, GetPointerShare(self, widget, args), IsFine,
        UIValueCapability_GetSettings(&self->Value)->FineFactor);
    return UIValueCapability_MoveTo(&self->Value, widget, GetValueAtShare(self, Share), NULL);
}

/* A key's move: an arrow by a step (up and right higher, down and left lower; Shift a fine one), Home and End to the track's ends.
 * Returns false for another key. */
static bool TryGetKeyTarget(const UISliderCapability* self, const UIScreen* screen, const WidgetKeyboardInputArgs* args, bool* outIsRepeatable,
    double* outTarget)
{
    const UIValueSettings* Settings = UIValueCapability_GetSettings(&self->Value);
    double Value = UIValueCapability_GetValue(&self->Value);
    bool IsFine = HasModifier(args->Modifiers, GAME_KEY_MODIFIER_SHIFT);
    GameKey Key = args->Key;
    *outIsRepeatable = true;
    if (UIElement_IsActionKey(screen, GameAction_UILeft, Key) || UIElement_IsActionKey(screen, GameAction_UIDown, Key))
    {
        *outTarget = UIValueSettings_Step(Settings, Value, -1.0, IsFine);
        return true;
    }
    if (UIElement_IsActionKey(screen, GameAction_UIRight, Key) || UIElement_IsActionKey(screen, GameAction_UIUp, Key))
    {
        *outTarget = UIValueSettings_Step(Settings, Value, 1.0, IsFine);
        return true;
    }
    *outIsRepeatable = false;
    if ((Key == GameKey_Home) || (Key == GameKey_End))
    {
        *outTarget = GetValueAtShare(self, (Key == GameKey_End) ? 1.0 : 0.0);
        return true;
    }
    return false;
}

/* Back to the default: a whole change. */
static Error ResetToDefault(UISliderCapability* self, Widget* widget)
{
    const UIValueSettings* Settings = UIValueCapability_GetSettings(&self->Value);
    return UIValueCapability_ApplyWholeChange(&self->Value, widget, UIValueSettings_Normalize(Settings, Settings->Default), NULL);
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static UISliderCapability* GetCapability(void* widget)
{
    return &((UISliderLookBase*)widget)->Slider;
}

static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static void* ResolveValue(void* widget)
{
    return &GetCapability(widget)->Value;
}

/* Code set the value or a setting: a drag goes on from the new value, on the track around it. */
static Error Value_OnChangedByCode(Widget* widget)
{
    UISliderCapability* Self = GetCapability(widget);
    if (Self->_isDragging)
    {
        HoldDragTrack(Self);
        Self->_drag.Share = UISliderCapability_GetShare(Self);
        Self->_drag.Offset = Self->_drag.Share - Self->_drag.PointerShare;
    }
    return Error_CreateSuccess();
}

/* Disabled, it takes a change in progress back and takes no focus. */
static Error Value_OnEnabledChanged(Widget* widget)
{
    UISliderCapability* Self = GetCapability(widget);
    bool IsEnabled = UIValueCapability_IsEnabled(&Self->Value);
    if (!IsEnabled)
    {
        Self->_isMiddlePressed = false;
        if (IsInSession(Self))
        {
            uint64_t Id = Widget_GetId(widget);
            Error Result = CancelSession(Self, widget);
            if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(Widget_GetScreen(widget), Id, &widget))
            {
                return Result;
            }
        }
    }
    return Widget_SetIsFocusable(widget, IsEnabled);
}

static const UIValueHooks SliderValueHooks =
{
    .OnChangedByCode = Value_OnChangedByCode,
    .OnEnabledChanged = Value_OnEnabledChanged,
    .OnPartChanged = NULL,
    .GetUnit = NULL
};

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UISlider: the arguments are required.");
    }
    return UISliderCapability_Construct(GetCapability(widget), widget, args);
}

static Error Behaviour_Deconstruct(void* widget)
{
    UISliderCapability_Deconstruct(GetCapability(widget));
    return Error_CreateSuccess();
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    (void)time;
    return UISliderCapability_Update(GetCapability(widget), widget);
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    return UISliderCapability_OnMouseInput(GetCapability(widget), widget, args);
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    return UISliderCapability_OnPressLost(GetCapability(widget), widget, button);
}

static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    return UISliderCapability_OnScrollInput(GetCapability(widget), widget, args, outIsHandled);
}

static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    return UISliderCapability_OnKeyboardInput(GetCapability(widget), widget, args, outIsHandled);
}

static Error Behaviour_OnLeave(void* widget, UILeaveReason reason)
{
    return UISliderCapability_OnLeave(GetCapability(widget), widget, reason);
}


// Fields.
static const UIKindCapability SliderCapabilities[] =
{
    { .Capability = UIElementCapability_Slider, .Resolve = ResolveCapability },
    { .Capability = UIElementCapability_Value, .Resolve = ResolveValue }
};

static const UIKindBehaviour SliderBehaviour =
{
    .Kind = UIElementKind_Slider,
    .Capabilities = SliderCapabilities,
    .CapabilityCount = sizeof(SliderCapabilities) / sizeof(SliderCapabilities[0]),
    .BaseSize = sizeof(UISliderLookBase),
    .IsFocusable = true,
    .NeedsEntering = true,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = Behaviour_Update,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnScrollInput = Behaviour_OnScrollInput,
    .OnKeyboardInput = Behaviour_OnKeyboardInput,
    .OnLeave = Behaviour_OnLeave
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UISlider_GetKindBehaviour(void)
{
    return &SliderBehaviour;
}


// Public functions: calling code.
Error UISlider_SetOrientation(UIScreen* screen, uint64_t widgetId, UISliderOrientation orientation)
{
    Widget* Target = NULL;
    UISliderCapability* Slider = NULL;
    Error Result = ResolveSlider(screen, widgetId, &Target, &Slider);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsOrientationValid(orientation))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UISlider_SetOrientation: the orientation is none of UISliderOrientation.");
    }
    if (Slider->_orientation != orientation)
    {
        Slider->_orientation = orientation;
        Widget_InvalidateMeasure(Target);
    }
    return Error_CreateSuccess();
}

Error UISlider_GetOrientation(UIScreen* screen, uint64_t widgetId, UISliderOrientation* outOrientation)
{
    Widget* Target = NULL;
    UISliderCapability* Slider = NULL;
    Error Result = ResolveSlider(screen, widgetId, &Target, &Slider);
    if (Result.Code == ErrorCode_Success)
    {
        *outOrientation = Slider->_orientation;
    }
    return Result;
}


// Public functions: the drag.
void UISliderDrag_Begin(UISliderDrag* self, double share, double offset, double pointerShare, double edgeMargin, bool isFine)
{
    self->Share = ClampShare(share);
    self->Offset = IsDoubleFinite(offset) ? offset : 0.0;
    self->PointerShare = IsDoubleFinite(pointerShare) ? pointerShare : 0.0;
    self->EdgeMargin = (edgeMargin > 0.0) && IsDoubleFinite(edgeMargin) ? edgeMargin : 0.0;
    self->IsFine = isFine;
}

double UISliderDrag_Move(UISliderDrag* self, double pointerShare, bool isFine, double fineFactor)
{
    double Pointer = IsDoubleFinite(pointerShare) ? pointerShare : self->PointerShare;
    if (isFine != self->IsFine)
    {
        // Shift changed: the rate changes from here, at the pointer's last place, so the thumb stays where it is.
        self->Offset = self->Share - self->PointerShare;
        self->IsFine = isFine;
    }
    if (isFine)
    {
        double Factor = ((fineFactor > 0.0) && (fineFactor <= 1.0)) ? fineFactor : 1.0;
        self->Share = ClampShare(self->Share + ((Pointer - self->PointerShare) * Factor));
    }
    else
    {
        double Unclamped = Pointer + self->Offset;
        self->Share = ClampShare(Unclamped);
        if ((Unclamped < 0.0) || (Unclamped > 1.0))
        {
            // Held at an end: the offset gives way, so turning back moves the thumb at once; with the pointer past that end of the box too,
            // the thumb is under the pointer again from here on. Only where the thumb already is: no jump, even after Shift let go out there.
            bool IsPastEnd = (Unclamped < 0.0) ? (Pointer < -self->EdgeMargin) : (Pointer > (1.0 + self->EdgeMargin));
            self->Offset = IsPastEnd ? 0.0 : (self->Share - Pointer);
        }
    }
    self->PointerShare = Pointer;
    return self->Share;
}


// Public functions: behaviour helpers.
Error UISliderCapability_Construct(UISliderCapability* self, Widget* widget, const UISliderArgs* args)
{
    const UISliderOptions* Options = args->Options;
    if (!IsOrientationValid(Options->Orientation))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UISlider: the orientation is none of UISliderOrientation.");
    }
    Error Result = UIValueCapability_Construct(&self->Value, &Options->Value, &SliderValueHooks, true);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        UIValueCapability_Deconstruct(&self->Value);
        return Result;
    }
    self->_orientation = Options->Orientation;
    self->_isDragging = false;
    UISliderDrag_Begin(&self->_drag, 0.0, 0.0, 0.0, 0.0, false);
    self->_sessionKey = GameKey_Unknown;
    self->_endedKey = GameKey_Unknown;
    self->_isMiddlePressed = false;
    self->_thumbLength = 0.0f;
    self->_hasDragTrack = false;
    self->_dragTrackMinimum = 0.0;
    self->_dragTrackMaximum = 0.0;
    if (!Options->Value.IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

void UISliderCapability_Deconstruct(UISliderCapability* self)
{
    UIValueCapability_Deconstruct(&self->Value);
}

Error UISliderCapability_Update(UISliderCapability* self, Widget* widget)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    // A key's release that cannot reach the slider any more (it was detached or disabled for input meanwhile) still ends the change, and so
    // does a button that is up, in a widget type that does not answer OnPressLost.
    const GameInput* Input = UIScreen_GetInput(Screen);
    if (Input == NULL)
    {
        return Error_CreateSuccess();
    }
    bool HasDragEnded = self->_isDragging && ((GameInput_GetRawKeyState(Input, GameKey_MouseLeft) & GAME_INPUT_KEY_STATE_DOWN) == 0U);
    bool HasKeyEnded = (self->_sessionKey != GameKey_Unknown)
        && ((GameInput_GetRawKeyState(Input, self->_sessionKey) & GAME_INPUT_KEY_STATE_DOWN) == 0U);
    return (HasDragEnded || HasKeyEnded) ? EndSession(self, widget) : Error_CreateSuccess();
}

UIElementState UISliderCapability_GetState(const UISliderCapability* self, const Widget* widget)
{
    return UIElementState_Choose(UIValueCapability_IsEnabled(&self->Value), self->_isDragging, false, Widget_IsEntered(widget), false,
        Widget_IsHovered(widget), Widget_IsFocused(widget));
}

double UISliderCapability_GetShare(const UISliderCapability* self)
{
    double Minimum = 0.0;
    double Maximum = 0.0;
    GetTrack(self, &Minimum, &Maximum);
    return UIValueSettings_ToShare(UIValueCapability_GetSettings(&self->Value), Minimum, Maximum, UIValueCapability_GetValue(&self->Value));
}

void UISliderCapability_SetThumbLength(UISliderCapability* self, float length)
{
    self->_thumbLength = ((length > 0.0f) && !Math_IsInfinityFloat(length)) ? length : 0.0f;
}

Error UISliderCapability_OnMouseInput(UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    bool IsEnabled = UIValueCapability_IsEnabled(&self->Value);
    Error EventResult = UIElement_PostMouseCues(widget, args, IsEnabled);
    if ((EventResult.Code != ErrorCode_Success) || !IsEnabled)
    {
        return EventResult;
    }
    EventResult = UIElement_PostMouseEvents(widget, args);
    if (EventResult.Code != ErrorCode_Success)
    {
        return EventResult;
    }
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            if (args->Button == GameKey_MouseMiddle)
            {
                self->_isMiddlePressed = !IsInSession(self);
                return Error_CreateSuccess();
            }
            if ((args->Button != GameKey_MouseLeft) || IsInSession(self))
            {
                return Error_CreateSuccess();
            }
            return BeginDrag(self, widget, args);
        case UIMouseInputType_Move:
            return ((args->Button == GameKey_MouseLeft) && self->_isDragging) ? MoveDrag(self, widget, args) : Error_CreateSuccess();
        case UIMouseInputType_ButtonRelease:
            if ((args->Button == GameKey_MouseMiddle) && self->_isMiddlePressed)
            {
                // A middle press let go on it puts it back to its default.
                self->_isMiddlePressed = false;
                bool IsOver = (args->WidgetPosition.x >= 0.0f) && (args->WidgetPosition.x <= 1.0f) && (args->WidgetPosition.y >= 0.0f)
                    && (args->WidgetPosition.y <= 1.0f);
                bool HasDefault = UIValueCapability_GetSettings(&self->Value)->HasDefault;
                return (IsOver && HasDefault && !IsInSession(self)) ? ResetToDefault(self, widget) : Error_CreateSuccess();
            }
            return ((args->Button == GameKey_MouseLeft) && self->_isDragging) ? EndSession(self, widget) : Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

Error UISliderCapability_OnPressLost(UISliderCapability* self, Widget* widget, GameKey button)
{
    if (button == GameKey_MouseMiddle)
    {
        self->_isMiddlePressed = false;
        return Error_CreateSuccess();
    }
    return ((button == GameKey_MouseLeft) && self->_isDragging) ? EndSession(self, widget) : Error_CreateSuccess();
}

Error UISliderCapability_OnLeave(UISliderCapability* self, Widget* widget, UILeaveReason reason)
{
    if (!IsInSession(self))
    {
        return Error_CreateSuccess();
    }
    // Left, the slider's keys are not its own any more, so its change ends here; what is left of the drag or the key does nothing.
    GameKey Key = self->_sessionKey;
    Error Result = (reason == UILeaveReason_Cancel) ? CancelSession(self, widget) : EndSession(self, widget);
    self->_endedKey = Key;
    return Result;
}

Error UISliderCapability_OnScrollInput(UISliderCapability* self, Widget* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    if (!Widget_IsEntered(widget) && !HasModifier(args->Modifiers, GAME_KEY_MODIFIER_CONTROL))
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    if (!UIValueCapability_IsEnabled(&self->Value) || IsInSession(self))
    {
        return Error_CreateSuccess();
    }
    double Target = UIValueSettings_Step(UIValueCapability_GetSettings(&self->Value), UIValueCapability_GetValue(&self->Value),
        (double)args->ScrollDelta.y, HasModifier(args->Modifiers, GAME_KEY_MODIFIER_SHIFT));
    return UIValueCapability_ApplyWholeChange(&self->Value, widget, Target, NULL);
}

Error UISliderCapability_OnKeyboardInput(UISliderCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    if ((args->Type == UIKeyboardInputType_Text) || !UIValueCapability_IsEnabled(&self->Value))
    {
        return Error_CreateSuccess();
    }
    if (args->Key == self->_endedKey)
    {
        if (args->Type != UIKeyboardInputType_KeyPress)
        {
            *outIsHandled = true;
            return Error_CreateSuccess();
        }
        self->_endedKey = GameKey_Unknown; // Pressed anew: a key like any other again.
    }
    if (args->Type == UIKeyboardInputType_KeyRelease)
    {
        if (args->Key == self->_sessionKey)
        {
            *outIsHandled = true;
            return EndSession(self, widget);
        }
        return Error_CreateSuccess();
    }
    if ((args->Type == UIKeyboardInputType_KeyPress) && IsInSession(self) && UIElement_IsActionKey(Screen, GameAction_UIBack, args->Key))
    {
        *outIsHandled = true;
        return CancelSession(self, widget);
    }
    if ((args->Type == UIKeyboardInputType_KeyPress) && UIValueCapability_GetSettings(&self->Value)->HasDefault
        && UIElement_IsActionPress(Screen, GameAction_UIReset, args->Key))
    {
        *outIsHandled = true;
        return ResetToDefault(self, widget); // During a change, a whole change is refused: nothing happens.
    }
    if ((args->Type == UIKeyboardInputType_KeyPress) && UIElement_IsActionPress(Screen, GameAction_UICopy, args->Key))
    {
        *outIsHandled = true;
        return UIValueCapability_Copy(&self->Value, widget);
    }
    if ((args->Type == UIKeyboardInputType_KeyPress) && UIElement_IsActionPress(Screen, GameAction_UIPaste, args->Key))
    {
        *outIsHandled = true;
        return IsInSession(self) ? Error_CreateSuccess() : UIValueCapability_Paste(&self->Value, widget, NULL, NULL);
    }

    bool IsRepeatable = false;
    double Target = 0.0;
    if (!TryGetKeyTarget(self, Screen, args, &IsRepeatable, &Target))
    {
        return UIElement_OnContextKey(widget, args, outIsHandled);
    }
    *outIsHandled = true;
    if (self->_isDragging || ((args->Type == UIKeyboardInputType_KeyRepeat) && !IsRepeatable))
    {
        return Error_CreateSuccess();
    }
    if (self->_sessionKey == GameKey_Unknown)
    {
        self->_sessionKey = args->Key;
        UIValueCapability_BeginChange(&self->Value);
    }
    bool HasMoved = false;
    Error Result = UIValueCapability_MoveTo(&self->Value, widget, Target, &HasMoved);
    return ((Result.Code == ErrorCode_Success) && HasMoved) ? UIElement_PostCue(widget, UIElementCue_ValueStep) : Result;
}
