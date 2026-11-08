#include "UIToggle.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"


// Static functions.
static bool IsStateValid(UIToggleState state)
{
    switch (state)
    {
        case UIToggleState_Off:
        case UIToggleState_On:
        case UIToggleState_Mixed:
            return true;
    }
    return false;
}

static Error ResolveToggle(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIToggleCapability** outToggle)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIToggle: the widget is gone, or its screen has no kit.");
    }
    UIToggleCapability* Toggle = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Toggle));
    if (Toggle == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIToggle: the widget is not a toggle.");
    }
    *outWidget = Target;
    *outToggle = Toggle;
    return Error_CreateSuccess();
}

/* The player's click: off and on swap, mixed turns on; then the two events, in order, and the cue. */
static Error Activate(UIToggleCapability* self, Widget* widget)
{
    self->_state = (self->_state == UIToggleState_On) ? UIToggleState_Off : UIToggleState_On;
    UIPropertyValue Value = UIPropertyValue_FromBool(self->_state == UIToggleState_On);
    UIScreen* Screen = Widget_GetScreen(widget);
    Error Result = UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Changed, &Value);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Committed, &Value);
    }
    return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_Activate) : Result;
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static UIToggleCapability* GetCapability(void* widget)
{
    return &((UIToggleLookBase*)widget)->Toggle;
}

static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIToggle: the arguments are required.");
    }
    return UIToggleCapability_Construct(GetCapability(widget), widget, args);
}

static Error Behaviour_Deconstruct(void* widget)
{
    UIToggleCapability_Deconstruct(GetCapability(widget));
    return Error_CreateSuccess();
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    (void)time;
    UIToggleCapability_Update(GetCapability(widget), widget);
    return Error_CreateSuccess();
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    return UIToggleCapability_OnMouseInput(GetCapability(widget), widget, args);
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    UIToggleCapability_OnPressLost(GetCapability(widget), button);
    return Error_CreateSuccess();
}

static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    return UIToggleCapability_OnKeyboardInput(GetCapability(widget), widget, args, outIsHandled);
}


// Fields.
static const UIKindCapability ToggleCapabilities[] = { { .Capability = UIElementCapability_Toggle, .Resolve = ResolveCapability } };

static const UIKindBehaviour ToggleBehaviour =
{
    .Kind = UIElementKind_Toggle,
    .Capabilities = ToggleCapabilities,
    .CapabilityCount = sizeof(ToggleCapabilities) / sizeof(ToggleCapabilities[0]),
    .BaseSize = sizeof(UIToggleLookBase),
    .IsFocusable = true,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = Behaviour_Update,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnKeyboardInput = Behaviour_OnKeyboardInput
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIToggle_GetKindBehaviour(void)
{
    return &ToggleBehaviour;
}


// Public functions: calling code.
Error UIToggle_SetState(UIScreen* screen, uint64_t widgetId, UIToggleState state)
{
    Widget* Target = NULL;
    UIToggleCapability* Toggle = NULL;
    Error Result = ResolveToggle(screen, widgetId, &Target, &Toggle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!IsStateValid(state))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToggle_SetState: an unknown state.");
    }
    Toggle->_state = state;
    return Error_CreateSuccess();
}

Error UIToggle_GetState(UIScreen* screen, uint64_t widgetId, UIToggleState* outState)
{
    Widget* Target = NULL;
    UIToggleCapability* Toggle = NULL;
    Error Result = ResolveToggle(screen, widgetId, &Target, &Toggle);
    if (Result.Code == ErrorCode_Success)
    {
        *outState = Toggle->_state;
    }
    return Result;
}

Error UIToggle_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text)
{
    Widget* Target = NULL;
    UIToggleCapability* Toggle = NULL;
    Error Result = ResolveToggle(screen, widgetId, &Target, &Toggle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool HasChanged = false;
    Result = UITextContent_Set(&Toggle->_text, text, &HasChanged);
    if ((Result.Code == ErrorCode_Success) && HasChanged)
    {
        Widget_InvalidateMeasure(Target);
    }
    return Result;
}

Error UIToggle_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UIToggleCapability* Toggle = NULL;
    Error Result = ResolveToggle(screen, widgetId, &Target, &Toggle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Toggle->_isEnabled = isEnabled;
    if (!isEnabled)
    {
        UIPressable_Cancel(&Toggle->_press);
    }
    return Widget_SetIsFocusable(Target, isEnabled);
}

Error UIToggle_SetClickMode(UIScreen* screen, uint64_t widgetId, UIClickMode clickMode)
{
    Widget* Target = NULL;
    UIToggleCapability* Toggle = NULL;
    Error Result = ResolveToggle(screen, widgetId, &Target, &Toggle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!UIPressable_AreSettingsValid(clickMode, Toggle->_press._activationButtons))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToggle_SetClickMode: an unknown click mode.");
    }
    Toggle->_press._clickMode = clickMode;
    return Error_CreateSuccess();
}

Error UIToggle_SetActivationButtons(UIScreen* screen, uint64_t widgetId, uint8_t activationButtons)
{
    Widget* Target = NULL;
    UIToggleCapability* Toggle = NULL;
    Error Result = ResolveToggle(screen, widgetId, &Target, &Toggle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!UIPressable_AreSettingsValid(Toggle->_press._clickMode, activationButtons))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToggle_SetActivationButtons: a bit that is no mouse button.");
    }
    Toggle->_press._activationButtons = activationButtons;
    return Error_CreateSuccess();
}


// Public functions: behaviour helpers.
Error UIToggleCapability_Construct(UIToggleCapability* self, Widget* widget, const UIToggleArgs* args)
{
    const UIToggleOptions* Options = args->Options;
    if (!IsStateValid(Options->State) || !UIPressable_AreSettingsValid(Options->ClickMode, Options->ActivationButtons))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIToggle: an unknown state or click mode, or a bit that is no mouse button.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UITextContent_Construct(&self->_text, Options->Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    self->_press = UIPressable_Create();
    self->_press._clickMode = Options->ClickMode;
    self->_press._activationButtons = Options->ActivationButtons;
    self->_state = Options->State;
    self->_isEnabled = Options->IsEnabled;
    if (!Options->IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

void UIToggleCapability_Deconstruct(UIToggleCapability* self)
{
    UITextContent_Deconstruct(&self->_text);
}

void UIToggleCapability_Update(UIToggleCapability* self, Widget* widget)
{
    UIPressable_Update(&self->_press, Widget_GetScreen(widget));
}

UIElementState UIToggleCapability_GetState(const UIToggleCapability* self, const Widget* widget)
{
    return UIElementState_Choose(self->_isEnabled, UIPressable_IsPressed(&self->_press), false, Widget_IsEntered(widget), false,
        Widget_IsHovered(widget), Widget_IsFocused(widget));
}

Error UIToggleCapability_OnMouseInput(UIToggleCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    Error Result = self->_isEnabled ? UIElement_PostMouseEvents(widget, args) : Error_CreateSuccess();
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElement_PostMouseCues(widget, args, self->_isEnabled);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsActivated = false;
    UIPressable_OnMouseInput(&self->_press, widget, args, self->_isEnabled, &IsActivated);
    return IsActivated ? Activate(self, widget) : Error_CreateSuccess();
}

void UIToggleCapability_OnPressLost(UIToggleCapability* self, GameKey button)
{
    UIPressable_OnPressLost(&self->_press, button);
}

Error UIToggleCapability_OnKeyboardInput(UIToggleCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled)
{
    bool IsActivated = false;
    UIPressable_OnKeyboardInput(&self->_press, widget, args, self->_isEnabled, outIsHandled, &IsActivated);
    if (IsActivated)
    {
        return Activate(self, widget);
    }
    return (self->_isEnabled && !*outIsHandled) ? UIElement_OnContextKey(widget, args, outIsHandled) : Error_CreateSuccess();
}
