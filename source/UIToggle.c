#include "UIToggle.h"
#include "UIKit.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"


// Macros.
/** How far the check stands inside the box, in UI pixels, and how much of the box's height a mixed state's bar takes. */
#define CHECK_INSET 3.0f
#define MIXED_BAR_SHARE 0.25f


// Types.
/* The standard toggle widget. */
typedef struct UIToggleWidgetStruct
{
    Widget Base;
    UIToggleCapability Toggle;
} UIToggleWidget;


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

/* The player's click: off and on swap, mixed turns on; then the two events, in order. */
static Error Activate(UIToggleCapability* self, Widget* widget)
{
    self->_state = (self->_state == UIToggleState_On) ? UIToggleState_Off : UIToggleState_On;
    UIPropertyValue Value = UIPropertyValue_FromBool(self->_state == UIToggleState_On);
    UIScreen* Screen = Widget_GetScreen(widget);
    Error Result = UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Changed, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Committed, &Value);
}


// Static functions: the standard widget's hooks.
static Error UIToggleWidget_ConstructVT(void* widget, void* args)
{
    UIToggleWidget* Toggle = widget;
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIToggle: the arguments are required.");
    }
    return UIToggleCapability_Construct(&Toggle->Toggle, &Toggle->Base, args);
}

static Error UIToggleWidget_OnDeconstructVT(void* self)
{
    return UIToggleCapability_Deconstruct(&((UIToggleWidget*)self)->Toggle);
}

static Error UIToggleWidget_UpdateVT(void* self, ProgramTime time)
{
    UIToggleWidget* Toggle = self;
    return UIToggleCapability_Update(&Toggle->Toggle, &Toggle->Base, time);
}

static Error UIToggleWidget_MeasureVT(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)context;
    (void)availableWidth;
    UIToggleWidget* Toggle = self;
    return UIToggleCapability_Measure(&Toggle->Toggle, &Toggle->Base, outSize);
}

/* The box at the left, centred across, with its check; the text after it. */
static Error UIToggleWidget_RenderVT(void* self, UIRenderContext* context)
{
    UIToggleWidget* Toggle = self;
    const UIToggleCapability* Capability = &Toggle->Toggle;
    UIScreen* Screen = Widget_GetScreen(&Toggle->Base);
    float Scale = UIScreen_GetScale(Screen);
    const UIThemeStyle* Style = UIElementLook_GetStyle(&Capability->_look, Screen);
    Rectangle Whole = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Whole))
    {
        return Error_CreateSuccess();
    }
    Vector2 BoxSize = UIThemeStyle_GetVector(Style, UIElementKind_Toggle, UITogglePart_BoxSize);
    Rectangle Box =
    {
        .x = Whole.x,
        .y = Whole.y + Math_FloorFloat(((Whole.height - (BoxSize.y * Scale)) * 0.5f) + 0.5f),
        .width = Math_FloorFloat((BoxSize.x * Scale) + 0.5f),
        .height = Math_FloorFloat((BoxSize.y * Scale) + 0.5f)
    };
    Error Result = UIElementLook_DrawBox(&Capability->_look, Style, UITogglePart_Box, context, Box, Scale);
    if ((Result.Code == ErrorCode_Success) && (Capability->_state != UIToggleState_Off))
    {
        float Inset = Math_FloorFloat((CHECK_INSET * Scale) + 0.5f);
        Rectangle Check = { .x = Box.x + Inset, .y = Box.y + Inset, .width = Math_MaxFloat(0.0f, Box.width - (2.0f * Inset)),
            .height = Math_MaxFloat(0.0f, Box.height - (2.0f * Inset)) };
        if (Capability->_state == UIToggleState_Mixed)
        {
            float Bar = Math_MaxFloat(1.0f, Math_FloorFloat((Box.height * MIXED_BAR_SHARE) + 0.5f));
            Check.y = Box.y + Math_FloorFloat(((Box.height - Bar) * 0.5f) + 0.5f);
            Check.height = Bar;
        }
        Result = UIElementLook_DrawBox(&Capability->_look, Style, UITogglePart_Check, context, Check, Scale);
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Vector2 Text = UIElementText_GetPixelSize(&Capability->_text, Screen);
    float TextX = Box.x + Box.width + Math_FloorFloat((UIThemeStyle_GetNumber(Style, UIElementKind_Toggle, UITogglePart_Spacing) * Scale) + 0.5f);
    float TextY = Whole.y + Math_FloorFloat(((Whole.height - Text.y) * 0.5f) + 0.5f);
    Color TextColor = UIElementLook_BlendColors(&Capability->_look, UIThemeStyle_GetColors(Style, UITogglePart_Text), WHITE);
    return UIElementText_Draw(&Capability->_text, context, TextX, TextY, TextColor);
}

static Error UIToggleWidget_OnMouseInputVT(void* self, const WidgetMouseInputArgs* args)
{
    UIToggleWidget* Toggle = self;
    return UIToggleCapability_OnMouseInput(&Toggle->Toggle, &Toggle->Base, args);
}

static Error UIToggleWidget_OnPressLostVT(void* self, GameKey button)
{
    UIToggleCapability_OnPressLost(&((UIToggleWidget*)self)->Toggle, button);
    return Error_CreateSuccess();
}

static Error UIToggleWidget_OnKeyboardInputVT(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIToggleWidget* Toggle = self;
    return UIToggleCapability_OnKeyboardInput(&Toggle->Toggle, &Toggle->Base, args, outIsHandled);
}

static void* ResolveLook(void* widget)
{
    return &((UIToggleWidget*)widget)->Toggle._look;
}

static void* ResolveCapability(void* widget)
{
    return &((UIToggleWidget*)widget)->Toggle;
}


// Fields.
static const WidgetVTable UIToggleWidgetVTable =
{
    .OnDeconstruct = UIToggleWidget_OnDeconstructVT,
    .Update = UIToggleWidget_UpdateVT,
    .Render = UIToggleWidget_RenderVT,
    .OnMouseInput = UIToggleWidget_OnMouseInputVT,
    .OnPressLost = UIToggleWidget_OnPressLostVT,
    .OnKeyboardInput = UIToggleWidget_OnKeyboardInputVT,
    .Measure = UIToggleWidget_MeasureVT
};


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
    return (Result.Code == ErrorCode_Success) ? UIElementText_Set(&Toggle->_text, Target, text) : Result;
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
    Error Result = UIElementLook_Construct(&self->_look, UIElementKind_Toggle, Options->Element.Variant);
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
    self->_state = Options->State;
    self->_isEnabled = Options->IsEnabled;
    if (!Options->IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

Error UIToggleCapability_Deconstruct(UIToggleCapability* self)
{
    return UIElementText_Deconstruct(&self->_text);
}

Error UIToggleCapability_Update(UIToggleCapability* self, Widget* widget, ProgramTime time)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIPressable_Update(&self->_press, Screen);
    UIElementState State = UIElementState_Choose(self->_isEnabled, UIPressable_IsPressed(&self->_press), false, false,
        Widget_IsHovered(widget), Widget_IsFocused(widget));
    UIElementLook_Update(&self->_look, Screen, State, time.PassedTime);
    return UIElementText_Refresh(&self->_text, Screen);
}

Error UIToggleCapability_OnMouseInput(UIToggleCapability* self, Widget* widget, const WidgetMouseInputArgs* args)
{
    Error Result = self->_isEnabled ? UIElement_PostMouseEvents(widget, args) : Error_CreateSuccess();
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

Error UIToggleCapability_Measure(UIToggleCapability* self, Widget* widget, Vector2* outSize)
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
    Vector2 BoxSize = UIThemeStyle_GetVector(Style, UIElementKind_Toggle, UITogglePart_BoxSize);
    Vector2 Minimum = UIThemeStyle_GetVector(Style, UIElementKind_Toggle, UITogglePart_MinSize);
    float Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_Toggle, UITogglePart_Spacing);
    outSize->x = Math_MaxFloat(BoxSize.x + Spacing + (Text.x / Scale), Minimum.x);
    outSize->y = Math_MaxFloat(Math_MaxFloat(BoxSize.y, Text.y / Scale), Minimum.y);
    return Error_CreateSuccess();
}

Error UIToggle_RegisterType(UIWidgetFactory* factory, uint64_t lookCapabilityId, uint64_t toggleCapabilityId, uint64_t* outTypeId)
{
    WidgetCapabilityEntry Capabilities[] =
    {
        { .CapabilityId = lookCapabilityId, .Resolver = ResolveLook },
        { .CapabilityId = toggleCapabilityId, .Resolver = ResolveCapability }
    };
    WidgetTypeDescriptor Descriptor =
    {
        .Name = u8"Toggle",
        .StructSize = sizeof(UIToggleWidget),
        .VTable = &UIToggleWidgetVTable,
        .Constructor = UIToggleWidget_ConstructVT,
        .Capabilities = Capabilities,
        .CapabilityCount = sizeof(Capabilities) / sizeof(Capabilities[0]),
        .Properties = NULL,
        .PropertyCount = 0U,
        .IsFocusable = true
    };
    return UIWidgetFactory_RegisterType(factory, &Descriptor, outTypeId);
}
