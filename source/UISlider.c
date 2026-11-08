#include "UISlider.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Macros.
/** With no step, the arrows and the wheel move this share of the range. */
#define DEFAULT_STEP_SHARE 0.01
/** How far from a whole step, in steps, a value is still on it: float noise (0.1 x 3) does not make it lie between two. */
#define ON_STEP_TOLERANCE 1e-9


// Static functions: small helpers.
static bool IsDoubleFinite(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value);
}

static bool IsRangeValid(double minimum, double maximum)
{
    return IsDoubleFinite(minimum) && IsDoubleFinite(maximum) && (minimum < maximum) && IsDoubleFinite(maximum - minimum);
}

/* Whether a step means anything: a positive finite number. */
static bool IsStepUsable(bool hasStep, double step)
{
    return hasStep && IsDoubleFinite(step) && (step > 0.0);
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

static Error PostValue(Widget* widget, UIElementEvent event, double value)
{
    UIPropertyValue Value = UIPropertyValue_FromDouble(value);
    return UIScreen_PostWidgetEvent(Widget_GetScreen(widget), Widget_GetId(widget), event, &Value);
}

/* The value becomes @p value, clamped and snapped; a change is reported. */
static Error MoveTo(UISliderCapability* self, Widget* widget, double value)
{
    double Next = UISlider_ClampAndSnap(value, self->_minimum, self->_maximum, self->_hasStep, self->_step);
    if (Next == self->_value)
    {
        return Error_CreateSuccess();
    }
    self->_value = Next;
    return PostValue(widget, UIElementEvent_Changed, Next);
}

/* A change that is whole at once: a wheel notch, a reset. */
static Error MoveWholeChange(UISliderCapability* self, Widget* widget, double value)
{
    double Before = self->_value;
    Error Result = MoveTo(self, widget, value);
    if ((Result.Code != ErrorCode_Success) || (self->_value == Before))
    {
        return Result;
    }
    Result = PostValue(widget, UIElementEvent_Committed, self->_value);
    return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_ValueStep) : Result;
}

/* The change in progress ends: committed unless the value ended where it began. */
static Error EndSession(UISliderCapability* self, Widget* widget)
{
    self->_isDragging = false;
    self->_sessionKey = GameKey_Unknown;
    return (self->_value != self->_sessionStartValue) ? PostValue(widget, UIElementEvent_Committed, self->_value)
        : Error_CreateSuccess();
}

/* The change in progress is taken back; while its key stays down, what is left of it does nothing. */
static Error CancelSession(UISliderCapability* self, Widget* widget)
{
    self->_endedKey = self->_sessionKey;
    self->_isDragging = false;
    self->_sessionKey = GameKey_Unknown;
    self->_value = self->_sessionStartValue;
    return PostValue(widget, UIElementEvent_Cancelled, self->_value);
}

static bool IsInSession(const UISliderCapability* self)
{
    return self->_isDragging || (self->_sessionKey != GameKey_Unknown);
}

/* The widget's width and the thumb's, in surface pixels, as layout put the box (a render transform turns the drawing and the
 * pointer alike, so the drag's arithmetic stays along the box). */
static void GetTrackPixels(const UISliderCapability* self, Widget* widget, double* outWidth, double* outThumb)
{
    *outWidth = (double)UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget).width;
    *outThumb = (double)self->_thumbLength * (double)Widget_GetEffectiveScale(widget);
}

/* The value the pointer means, less how far from the thumb's centre it holds the thumb. */
static double GetPointerValue(const UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    double Width = 0.0;
    double Thumb = 0.0;
    GetTrackPixels(self, widget, &Width, &Thumb);
    double Pointer = ((double)args->WidgetPosition.x * Width) - self->_grabOffsetPixels;
    double Fraction = UISlider_GetPointerFraction(Pointer, Width, Thumb);
    return self->_minimum + (Fraction * (self->_maximum - self->_minimum));
}

/* A press: on the thumb it holds the thumb where it was grabbed and changes nothing, on the track it brings the thumb under
 * the pointer. */
static Error BeginDrag(UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    double Width = 0.0;
    double Thumb = 0.0;
    GetTrackPixels(self, widget, &Width, &Thumb);
    double ThumbCentre = (Thumb * 0.5) + (UISlider_GetFraction(self->_value, self->_minimum, self->_maximum) * (Width - Thumb));
    double FromCentre = ((double)args->WidgetPosition.x * Width) - ThumbCentre;
    self->_isDragging = true;
    self->_sessionStartValue = self->_value;
    if (Math_AbsDouble(FromCentre) <= (Thumb * 0.5))
    {
        self->_grabOffsetPixels = FromCentre;
        return Error_CreateSuccess();
    }
    self->_grabOffsetPixels = 0.0;
    return MoveTo(self, widget, GetPointerValue(self, widget, args));
}

/* A key's move: an arrow by a step (up and right higher, down and left lower), Home and End to the ends. Returns false for
 * another key. */
static bool TryGetKeyTarget(const UISliderCapability* self, const UIScreen* screen, GameKey key, bool* outIsRepeatable, double* outTarget)
{
    *outIsRepeatable = true;
    if (UIElement_IsActionKey(screen, GameAction_UILeft, key) || UIElement_IsActionKey(screen, GameAction_UIDown, key))
    {
        *outTarget = UISlider_GetStepped(self->_value, self->_minimum, self->_maximum, self->_hasStep, self->_step, -1.0);
        return true;
    }
    if (UIElement_IsActionKey(screen, GameAction_UIRight, key) || UIElement_IsActionKey(screen, GameAction_UIUp, key))
    {
        *outTarget = UISlider_GetStepped(self->_value, self->_minimum, self->_maximum, self->_hasStep, self->_step, 1.0);
        return true;
    }
    *outIsRepeatable = false;
    if (key == GameKey_Home)
    {
        *outTarget = self->_minimum;
        return true;
    }
    if (key == GameKey_End)
    {
        *outTarget = self->_maximum;
        return true;
    }
    return false;
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

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UISlider: the arguments are required.");
    }
    return UISliderCapability_Construct(GetCapability(widget), widget, args);
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
static const UIKindCapability SliderCapabilities[] = { { .Capability = UIElementCapability_Slider, .Resolve = ResolveCapability } };

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
Error UISlider_SetValue(UIScreen* screen, uint64_t widgetId, double value)
{
    Widget* Target = NULL;
    UISliderCapability* Slider = NULL;
    Error Result = ResolveSlider(screen, widgetId, &Target, &Slider);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsDoubleFinite(value))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UISlider_SetValue: the value must be a finite number.");
    }
    Slider->_value = UISlider_ClampAndSnap(value, Slider->_minimum, Slider->_maximum, Slider->_hasStep, Slider->_step);
    return Error_CreateSuccess();
}

Error UISlider_GetValue(UIScreen* screen, uint64_t widgetId, double* outValue)
{
    Widget* Target = NULL;
    UISliderCapability* Slider = NULL;
    Error Result = ResolveSlider(screen, widgetId, &Target, &Slider);
    if (Result.Code == ErrorCode_Success)
    {
        *outValue = Slider->_value;
    }
    return Result;
}

Error UISlider_SetRange(UIScreen* screen, uint64_t widgetId, double minimum, double maximum)
{
    Widget* Target = NULL;
    UISliderCapability* Slider = NULL;
    Error Result = ResolveSlider(screen, widgetId, &Target, &Slider);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsRangeValid(minimum, maximum))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UISlider_SetRange: the ends must be finite and the upper above the lower, with a finite span.");
    }
    Slider->_minimum = minimum;
    Slider->_maximum = maximum;
    Slider->_value = UISlider_ClampAndSnap(Slider->_value, minimum, maximum, Slider->_hasStep, Slider->_step);
    Slider->_sessionStartValue = UISlider_ClampAndSnap(Slider->_sessionStartValue, minimum, maximum, Slider->_hasStep, Slider->_step);
    return Error_CreateSuccess();
}

Error UISlider_SetStep(UIScreen* screen, uint64_t widgetId, bool hasStep, double step)
{
    Widget* Target = NULL;
    UISliderCapability* Slider = NULL;
    Error Result = ResolveSlider(screen, widgetId, &Target, &Slider);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (hasStep && (!IsDoubleFinite(step) || !(step > 0.0)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UISlider_SetStep: the step must be a finite number above 0.");
    }
    Slider->_hasStep = hasStep;
    Slider->_step = step;
    Slider->_value = UISlider_ClampAndSnap(Slider->_value, Slider->_minimum, Slider->_maximum, hasStep, step);
    Slider->_sessionStartValue = UISlider_ClampAndSnap(Slider->_sessionStartValue, Slider->_minimum, Slider->_maximum, hasStep, step);
    return Error_CreateSuccess();
}

Error UISlider_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UISliderCapability* Slider = NULL;
    Error Result = ResolveSlider(screen, widgetId, &Target, &Slider);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Slider->_isEnabled = isEnabled;
    if (!isEnabled && IsInSession(Slider))
    {
        Result = CancelSession(Slider, Target);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Widget_SetIsFocusable(Target, isEnabled);
}

Error UISlider_SetDefault(UIScreen* screen, uint64_t widgetId, bool hasDefault, double value)
{
    Widget* Target = NULL;
    UISliderCapability* Slider = NULL;
    Error Result = ResolveSlider(screen, widgetId, &Target, &Slider);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (hasDefault && !IsDoubleFinite(value))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UISlider_SetDefault: the default must be a finite number.");
    }
    Slider->_hasDefault = hasDefault;
    Slider->_default = value;
    return Error_CreateSuccess();
}


// Public functions: behaviour helpers.
Error UISliderCapability_Construct(UISliderCapability* self, Widget* widget, const UISliderArgs* args)
{
    const UISliderOptions* Options = args->Options;
    if (!IsRangeValid(Options->Minimum, Options->Maximum) || !IsDoubleFinite(Options->Value)
        || (Options->HasStep && (!IsDoubleFinite(Options->Step) || !(Options->Step > 0.0)))
        || (Options->HasDefault && !IsDoubleFinite(Options->Default)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UISlider: the range, the value, the step or the default is not valid.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_minimum = Options->Minimum;
    self->_maximum = Options->Maximum;
    self->_hasStep = Options->HasStep;
    self->_step = Options->Step;
    self->_value = UISlider_ClampAndSnap(Options->Value, Options->Minimum, Options->Maximum, Options->HasStep, Options->Step);
    self->_isEnabled = Options->IsEnabled;
    self->_hasDefault = Options->HasDefault;
    self->_default = Options->Default;
    self->_isDragging = false;
    self->_sessionKey = GameKey_Unknown;
    self->_endedKey = GameKey_Unknown;
    self->_sessionStartValue = self->_value;
    self->_grabOffsetPixels = 0.0;
    self->_thumbLength = 0.0f;
    if (!Options->IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

Error UISliderCapability_Update(UISliderCapability* self, Widget* widget)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    // A key's release that cannot reach the slider any more (it was detached or disabled for input meanwhile) still ends the change, and so
    // does a button that is up, in a widget type that does not answer OnPressLost.
    const GameInput* Input = UIScreen_GetInput(Screen);
    Error Result = Error_CreateSuccess();
    if (Input != NULL)
    {
        bool HasDragEnded = self->_isDragging && ((GameInput_GetRawKeyState(Input, GameKey_MouseLeft) & GAME_INPUT_KEY_STATE_DOWN) == 0U);
        bool HasKeyEnded = (self->_sessionKey != GameKey_Unknown)
            && ((GameInput_GetRawKeyState(Input, self->_sessionKey) & GAME_INPUT_KEY_STATE_DOWN) == 0U);
        if (HasDragEnded || HasKeyEnded)
        {
            Result = EndSession(self, widget);
        }
    }
    return Result;
}

UIElementState UISliderCapability_GetState(const UISliderCapability* self, const Widget* widget)
{
    return UIElementState_Choose(self->_isEnabled, self->_isDragging, false, Widget_IsEntered(widget), false, Widget_IsHovered(widget),
        Widget_IsFocused(widget));
}

void UISliderCapability_SetThumbLength(UISliderCapability* self, float length)
{
    self->_thumbLength = ((length > 0.0f) && !Math_IsInfinityFloat(length)) ? length : 0.0f;
}

Error UISliderCapability_OnMouseInput(UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    Error EventResult = UIElement_PostMouseCues(widget, args, self->_isEnabled);
    if ((EventResult.Code != ErrorCode_Success) || !self->_isEnabled)
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
            if ((args->Button != GameKey_MouseLeft) || IsInSession(self))
            {
                return Error_CreateSuccess();
            }
            return BeginDrag(self, widget, args);
        case UIMouseInputType_Move:
            return ((args->Button == GameKey_MouseLeft) && self->_isDragging) ? MoveTo(self, widget, GetPointerValue(self, widget, args))
                : Error_CreateSuccess();
        case UIMouseInputType_ButtonRelease:
            return ((args->Button == GameKey_MouseLeft) && self->_isDragging) ? EndSession(self, widget) : Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

Error UISliderCapability_OnPressLost(UISliderCapability* self, Widget* widget, GameKey button)
{
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
    if (!Widget_IsEntered(widget) && ((args->Modifiers & GAME_KEY_MODIFIER_CONTROL) == 0U))
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    if (!self->_isEnabled || IsInSession(self))
    {
        return Error_CreateSuccess();
    }
    double Target = UISlider_GetStepped(self->_value, self->_minimum, self->_maximum, self->_hasStep, self->_step, (double)args->ScrollDelta.y);
    return MoveWholeChange(self, widget, Target);
}

Error UISliderCapability_OnKeyboardInput(UISliderCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    if ((args->Type == UIKeyboardInputType_Text) || !self->_isEnabled)
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
    if ((args->Type == UIKeyboardInputType_KeyPress) && self->_hasDefault && UIElement_IsActionPress(Screen, GameAction_UIReset, args->Key))
    {
        *outIsHandled = true;
        return IsInSession(self) ? Error_CreateSuccess() : MoveWholeChange(self, widget, self->_default);
    }

    bool IsRepeatable = false;
    double Target = 0.0;
    if (!TryGetKeyTarget(self, Screen, args->Key, &IsRepeatable, &Target))
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
        self->_sessionStartValue = self->_value;
    }
    double Before = self->_value;
    Error Result = MoveTo(self, widget, Target);
    return ((Result.Code == ErrorCode_Success) && (self->_value != Before)) ? UIElement_PostCue(widget, UIElementCue_ValueStep) : Result;
}

// Public functions: the value's arithmetic.
double UISlider_ClampAndSnap(double value, double minimum, double maximum, bool hasStep, double step)
{
    if (Math_IsNaNDouble(value) || (value <= minimum))
    {
        return minimum;
    }
    if (value >= maximum)
    {
        return maximum;
    }
    double Steps = IsStepUsable(hasStep, step) ? ((value - minimum) / step) : 0.0;
    if (!IsStepUsable(hasStep, step) || !IsDoubleFinite(Steps))
    {
        return value;
    }
    double Snapped = minimum + (Math_FloorDouble(Steps + 0.5) * step);
    if ((Snapped >= maximum) || ((maximum - value) < Math_AbsDouble(value - Snapped)))
    {
        return maximum; // Past the last whole step the end counts as one.
    }
    return Snapped;
}

double UISlider_GetStepped(double value, double minimum, double maximum, bool hasStep, double step, double count)
{
    double Value = UISlider_ClampAndSnap(value, minimum, maximum, false, 0.0);
    double Count = Math_IsNaNDouble(count) ? 0.0 : count;
    if (!IsStepUsable(hasStep, step))
    {
        return UISlider_ClampAndSnap(Value + (Count * (maximum - minimum) * DEFAULT_STEP_SHARE), minimum, maximum, false, 0.0);
    }
    double Steps = Math_FloorDouble(Math_AbsDouble(Count) + 0.5);
    double Position = (Value - minimum) / step;
    if ((Steps == 0.0) || !IsDoubleFinite(Position))
    {
        return Value;
    }
    double OnStep = Math_FloorDouble(Position + 0.5);
    bool IsOnStep = Math_AbsDouble(Position - OnStep) <= ON_STEP_TOLERANCE;
    double From = 0.0;
    double Target = 0.0;
    if (Count > 0.0)
    {
        From = IsOnStep ? OnStep : Math_FloorDouble(Position);
        Target = From + Steps;
    }
    else
    {
        From = IsOnStep ? OnStep : Math_CeilDouble(Position);
        Target = From - Steps;
    }
    double Next = minimum + (Target * step);
    return (Next <= minimum) ? minimum : ((Next >= maximum) ? maximum : Next);
}

double UISlider_GetFraction(double value, double minimum, double maximum)
{
    if (!(maximum > minimum))
    {
        return 0.0;
    }
    double Fraction = (value - minimum) / (maximum - minimum);
    return (Fraction > 0.0) ? ((Fraction < 1.0) ? Fraction : 1.0) : 0.0;
}

double UISlider_GetPointerFraction(double pointer, double boxLength, double thumbLength)
{
    double Travel = boxLength - thumbLength;
    if (!(Travel > 0.0))
    {
        return 0.0;
    }
    double Fraction = (pointer - (thumbLength * 0.5)) / Travel;
    return (Fraction > 0.0) ? ((Fraction < 1.0) ? Fraction : 1.0) : 0.0;
}
