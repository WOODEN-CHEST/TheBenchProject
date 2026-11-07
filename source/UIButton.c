#include "UIButton.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Types.
/* The standard button widget. */
typedef struct UIButtonWidgetStruct
{
    Widget Base;
    UIButtonCapability Button;
} UIButtonWidget;


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


// Static functions: the standard widget's hooks.
static Error UIButtonWidget_ConstructVT(void* widget, void* args)
{
    UIButtonWidget* Button = widget;
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIButton: the arguments are required.");
    }
    return UIButtonCapability_Construct(&Button->Button, &Button->Base, args);
}

static Error UIButtonWidget_OnDeconstructVT(void* self)
{
    return UIButtonCapability_Deconstruct(&((UIButtonWidget*)self)->Button);
}

static Error UIButtonWidget_UpdateVT(void* self, ProgramTime time)
{
    UIButtonWidget* Button = self;
    return UIButtonCapability_Update(&Button->Button, &Button->Base, time);
}

static Error UIButtonWidget_MeasureVT(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    UIButtonWidget* Button = self;
    return UIButtonCapability_Measure(&Button->Button, &Button->Base, outSize);
}

static Error UIButtonWidget_RenderVT(void* self, UIRenderContext* context)
{
    UIButtonWidget* Button = self;
    UIScreen* Screen = Widget_GetScreen(&Button->Base);
    Rectangle Box = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Box))
    {
        return Error_CreateSuccess();
    }
    Error Result = UIElementLook_DrawBox(&Button->Button._look, UIElementLook_GetStyle(&Button->Button._look, Screen),
        UIButtonPart_Background, context, Box, UIScreen_GetScale(Screen));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIButtonCapability_RenderText(&Button->Button, &Button->Base, context, Box);
}

static Error UIButtonWidget_OnMouseInputVT(void* self, const WidgetMouseInputArgs* args)
{
    UIButtonWidget* Button = self;
    return UIButtonCapability_OnMouseInput(&Button->Button, &Button->Base, args);
}

static Error UIButtonWidget_OnPressLostVT(void* self, GameKey button)
{
    UIButtonCapability_OnPressLost(&((UIButtonWidget*)self)->Button, button);
    return Error_CreateSuccess();
}

static Error UIButtonWidget_OnScrollInputVT(void* self, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIButtonWidget* Button = self;
    return UIButtonCapability_OnScrollInput(&Button->Button, &Button->Base, args, outIsHandled);
}

static Error UIButtonWidget_OnKeyboardInputVT(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIButtonWidget* Button = self;
    return UIButtonCapability_OnKeyboardInput(&Button->Button, &Button->Base, args, outIsHandled);
}

static void* ResolveLook(void* widget)
{
    return &((UIButtonWidget*)widget)->Button._look;
}

static void* ResolveCapability(void* widget)
{
    return &((UIButtonWidget*)widget)->Button;
}


// Fields.
static const WidgetVTable UIButtonWidgetVTable =
{
    .OnDeconstruct = UIButtonWidget_OnDeconstructVT,
    .Update = UIButtonWidget_UpdateVT,
    .Render = UIButtonWidget_RenderVT,
    .OnMouseInput = UIButtonWidget_OnMouseInputVT,
    .OnPressLost = UIButtonWidget_OnPressLostVT,
    .OnScrollInput = UIButtonWidget_OnScrollInputVT,
    .OnKeyboardInput = UIButtonWidget_OnKeyboardInputVT,
    .Measure = UIButtonWidget_MeasureVT
};


// Public functions: calling code.
Error UIButton_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    return (Result.Code == ErrorCode_Success) ? UIElementText_Set(&Button->_text, Target, text) : Result;
}

Error UIButton_GetText(UIScreen* screen, uint64_t widgetId, const unsigned char** outText)
{
    Widget* Target = NULL;
    UIButtonCapability* Button = NULL;
    Error Result = ResolveButton(screen, widgetId, &Target, &Button);
    if (Result.Code == ErrorCode_Success)
    {
        *outText = UIElementText_Get(&Button->_text);
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
    if (!UIPressable_AreSettingsValid(Options->ClickMode, Options->ActivationButtons))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIButton: an unknown click mode, or a bit that is no mouse button.");
    }
    Error Result = UIElementLook_Construct(&self->_look, UIElementKind_Button, Options->Element.Variant);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElement_ApplyOptions(widget, &Options->Element);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = UIElementText_Construct(&self->_text, UIKit_GetTextFactory(args->Kit), UIKit_GetAssets(args->Kit),
        UIKit_GetLogger(args->Kit), Options->Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
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

Error UIButtonCapability_Deconstruct(UIButtonCapability* self)
{
    return UIElementText_Deconstruct(&self->_text);
}

Error UIButtonCapability_Update(UIButtonCapability* self, Widget* widget, ProgramTime time)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIPressable_Update(&self->_press, Screen);
    UIElementState State = UIElementState_Choose(self->_isEnabled, UIPressable_IsPressed(&self->_press), false, Widget_IsEntered(widget),
        self->_isToggled, Widget_IsHovered(widget), Widget_IsFocused(widget));
    UIElementLook_Update(&self->_look, Screen, State, time.PassedTime);
    return UIElementText_Refresh(&self->_text, Screen);
}

Error UIButtonCapability_OnMouseInput(UIButtonCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    Error Result = self->_isEnabled ? UIElement_PostMouseEvents(widget, args) : Error_CreateSuccess();
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsActivated = false;
    UIPressable_OnMouseInput(&self->_press, widget, args, self->_isEnabled, &IsActivated);
    return IsActivated ? UIElement_PostActivatedByMouse(widget, args) : Error_CreateSuccess();
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
        return UIElement_PostActivatedByKey(widget, args);
    }
    return (self->_isEnabled && !*outIsHandled) ? UIElement_OnContextKey(widget, args, outIsHandled) : Error_CreateSuccess();
}

Error UIButtonCapability_Measure(UIButtonCapability* self, Widget* widget, Vector2* outSize)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    Error Result = UIElementText_Refresh(&self->_text, Screen);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    float Scale = UIScreen_GetScale(Screen);
    Vector2 Text = UIElementText_GetPixelSize(&self->_text, Screen);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&self->_look, Screen);
    Vector2 Padding = UIThemeStyle_GetVector(Style, UIElementKind_Button, UIButtonPart_Padding);
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_Button, UIButtonPart_MinSize);
    outSize->x = Math_MaxFloat((Text.x / Scale) + (2.0f * Padding.x), Minimum.x);
    outSize->y = Math_MaxFloat((Text.y / Scale) + (2.0f * Padding.y), Minimum.y);
    return Error_CreateSuccess();
}

Error UIButtonCapability_RenderText(const UIButtonCapability* self, Widget* widget, UIRenderContext* context, Rectangle box)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&self->_look, Screen);
    Color TextColor = UIElementLook_BlendColors(&self->_look, UIThemeStyle_GetColors(Style, UIButtonPart_Text), WHITE);
    Vector2 Text = UIElementText_GetPixelSize(&self->_text, Screen);
    bool IsTooLong = Text.x > box.width;
    float PaddingPixels = UIThemeStyle_GetVector(Style, UIElementKind_Button, UIButtonPart_Padding).x * UIScreen_GetScale(Screen);
    float X = IsTooLong ? (box.x + PaddingPixels) : (box.x + Math_FloorFloat(((box.width - Text.x) * 0.5f) + 0.5f));
    float Y = box.y + Math_FloorFloat(((box.height - Text.y) * 0.5f) + 0.5f);
    if (IsTooLong)
    {
        RenderContext_PushClip(UIRenderContext_GetRenderContext(context), box);
    }
    Error Result = UIElementText_Draw(&self->_text, context, X, Y, TextColor);
    if (IsTooLong)
    {
        RenderContext_PopClip(UIRenderContext_GetRenderContext(context));
    }
    return Result;
}

Error UIButton_RegisterType(UIWidgetFactory* factory, uint64_t lookCapabilityId, uint64_t buttonCapabilityId, uint64_t* outTypeId)
{
    WidgetCapabilityEntry Capabilities[] =
    {
        { .CapabilityId = lookCapabilityId, .Resolver = ResolveLook },
        { .CapabilityId = buttonCapabilityId, .Resolver = ResolveCapability }
    };
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"Button",
        .StructSize = sizeof(UIButtonWidget),
        .VTable = &UIButtonWidgetVTable,
        .Constructor = UIButtonWidget_ConstructVT,
        .Capabilities = Capabilities,
        .CapabilityCount = sizeof(Capabilities) / sizeof(Capabilities[0]),
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = true
    };
    return UIWidgetFactory_RegisterType(factory, &Descriptor, outTypeId);
}
