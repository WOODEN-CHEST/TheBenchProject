#include "UIButton.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"


// Static functions.
/* The capability of a widget id: a stale id or another kind of widget is refused. */
static Error ResolveButton(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIButtonCapability** outButton)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIButton: the widget is gone, or its screen has no kit.");
    }
    UIButtonCapability* Button = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Button));
    if (Button == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIButton: the widget is not a button.");
    }
    *outWidget = Target;
    *outButton = Button;
    return Error_CreateSuccess();
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static UIButtonCapability* GetCapability(void* widget)
{
    return &((UIButtonLookBase*)widget)->Button;
}

static void* ResolveCapability(void* widget)
{
    return GetCapability(widget);
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIButton: the arguments are required.");
    }
    return UIButtonCapability_Construct(GetCapability(widget), widget, args);
}

static Error Behaviour_Deconstruct(void* widget)
{
    UIButtonCapability_Deconstruct(GetCapability(widget));
    return Error_CreateSuccess();
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    (void)time;
    UIButtonCapability_Update(GetCapability(widget), widget);
    return Error_CreateSuccess();
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    return UIButtonCapability_OnMouseInput(GetCapability(widget), widget, args);
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    UIButtonCapability_OnPressLost(GetCapability(widget), button);
    return Error_CreateSuccess();
}

static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    return UIButtonCapability_OnScrollInput(GetCapability(widget), widget, args, outIsHandled);
}

static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    return UIButtonCapability_OnKeyboardInput(GetCapability(widget), widget, args, outIsHandled);
}


// Fields.
static const UIKindCapability ButtonCapabilities[] = { { .Capability = UIElementCapability_Button, .Resolve = ResolveCapability } };

static const UIKindBehaviour ButtonBehaviour =
{
    .Kind = UIElementKind_Button,
    .Capabilities = ButtonCapabilities,
    .CapabilityCount = sizeof(ButtonCapabilities) / sizeof(ButtonCapabilities[0]),
    .BaseSize = sizeof(UIButtonLookBase),
    .IsFocusable = true,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = Behaviour_Update,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnScrollInput = Behaviour_OnScrollInput,
    .OnKeyboardInput = Behaviour_OnKeyboardInput
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIButton_GetKindBehaviour(void)
{
    return &ButtonBehaviour;
}


// Public functions: calling code.
Error UIButton_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool HasChanged = false;
    Result = UITextContent_Set(&Button->_text, text, &HasChanged);
    if ((Result.Code == ErrorCode_Success) && HasChanged)
    {
        Widget_InvalidateMeasure(Target);
    }
    return Result;
}

Error UIButton_GetText(UIScreen* screen, uint64_t widgetId, const unsigned char** outText)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code == ErrorCode_Success)
    {
        *outText = UITextContent_Get(&Button->_text);
    }
    return Result;
}

Error UIButton_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Button->_isEnabled = isEnabled;
    if (!isEnabled)
    {
        UIPressable_Cancel(&Button->_press);
    }
    return Widget_SetIsFocusable(Target, isEnabled);
}

Error UIButton_IsEnabled(UIScreen* screen, uint64_t widgetId, bool* outIsEnabled)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsEnabled = Button->_isEnabled;
    }
    return Result;
}

Error UIButton_SetIsToggled(UIScreen* screen, uint64_t widgetId, bool isToggled)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code == ErrorCode_Success)
    {
        Button->_isToggled = isToggled;
    }
    return Result;
}

Error UIButton_IsToggled(UIScreen* screen, uint64_t widgetId, bool* outIsToggled)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsToggled = Button->_isToggled;
    }
    return Result;
}

Error UIButton_SetClickMode(UIScreen* screen, uint64_t widgetId, UIClickMode clickMode)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!UIPressable_AreSettingsValid(clickMode, Button->_press._activationButtons))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIButton_SetClickMode: an unknown click mode.");
    }
    Button->_press._clickMode = clickMode;
    return Error_CreateSuccess();
}

Error UIButton_SetActivationButtons(UIScreen* screen, uint64_t widgetId, uint8_t activationButtons)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!UIPressable_AreSettingsValid(Button->_press._clickMode, activationButtons))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIButton_SetActivationButtons: a bit that is no mouse button.");
    }
    Button->_press._activationButtons = activationButtons;
    return Error_CreateSuccess();
}

Error UIButton_SetIcon(UIScreen* screen, uint64_t widgetId, const UIElementIcon* icon)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((icon == NULL) || !UIElementIcon_IsValid(icon))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIButton_SetIcon: the icon is not valid.");
    }
    if (!UIElementIcon_Equals(&Button->_icon, icon))
    {
        Button->_icon = *icon;
        Widget_InvalidateMeasure(Target);
    }
    return Error_CreateSuccess();
}

Error UIButton_SetTakesWheel(UIScreen* screen, uint64_t widgetId, bool takesWheel)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code == ErrorCode_Success)
    {
        Button->_takesWheel = takesWheel;
    }
    return Result;
}


// Public functions: behaviour helpers.
Error UIButtonCapability_Construct(UIButtonCapability* self, Widget* widget, const UIButtonArgs* args)
{
    const UIButtonOptions* Options = args->Options;
    if (!UIPressable_AreSettingsValid(Options->ClickMode, Options->ActivationButtons) || !UIElementIcon_IsValid(&Options->Icon))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UIButton: an unknown click mode, a bit that is no mouse button, or an icon that is not valid.");
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
    self->_icon = Options->Icon;
    self->_press = UIPressable_Create();
    self->_press._clickMode = Options->ClickMode;
    self->_press._activationButtons = Options->ActivationButtons;
    self->_isEnabled = Options->IsEnabled;
    self->_isToggled = Options->IsToggled;
    self->_takesWheel = Options->TakesWheel;
    if (!Options->IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

void UIButtonCapability_Deconstruct(UIButtonCapability* self)
{
    UITextContent_Deconstruct(&self->_text);
}

void UIButtonCapability_Update(UIButtonCapability* self, Widget* widget)
{
    UIPressable_Update(&self->_press, Widget_GetScreen(widget));
}

UIElementState UIButtonCapability_GetState(const UIButtonCapability* self, const Widget* widget)
{
    return UIElementState_Choose(self->_isEnabled, UIPressable_IsPressed(&self->_press), false, Widget_IsEntered(widget),
        self->_isToggled, Widget_IsHovered(widget), Widget_IsFocused(widget));
}

Error UIButtonCapability_OnMouseInput(UIButtonCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
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
    if (!IsActivated)
    {
        return Error_CreateSuccess();
    }
    Result = UIElement_PostActivatedByMouse(widget, args);
    return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_Activate) : Result;
}

void UIButtonCapability_OnPressLost(UIButtonCapability* self, GameKey button)
{
    UIPressable_OnPressLost(&self->_press, button);
}

Error UIButtonCapability_OnScrollInput(UIButtonCapability* self, Widget* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    if (!self->_takesWheel)
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    if (!self->_isEnabled)
    {
        return Error_CreateSuccess();
    }
    UIEventMouse Mouse = { .Button = GameKey_Unknown, .Modifiers = args->Modifiers, .ScreenPosition = args->ScreenPosition,
        .ScrollDelta = args->ScrollDelta, .IsOverWidget = true };
    return UIScreen_PostWidgetMouseEvent(Widget_GetScreen(widget), Widget_GetId(widget), UIElementEvent_Scrolled, &Mouse);
}

Error UIButtonCapability_OnKeyboardInput(UIButtonCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled)
{
    bool IsActivated = false;
    UIPressable_OnKeyboardInput(&self->_press, widget, args, self->_isEnabled, outIsHandled, &IsActivated);
    if (IsActivated)
    {
        Error Result = UIElement_PostActivatedByKey(widget, args);
        return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(widget, UIElementCue_Activate) : Result;
    }
    return (self->_isEnabled && !*outIsHandled) ? UIElement_OnContextKey(widget, args, outIsHandled) : Error_CreateSuccess();
}
