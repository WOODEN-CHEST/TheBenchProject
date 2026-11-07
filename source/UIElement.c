#include "UIElement.h"
#include "NamespacedKey.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIRenderContext.h"
#include "UIScreen.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** The largest channel of a colour, as a float. */
#define CHANNEL_MAX 255.0f
/** The size of an element text's one string: the layout's multiplier is then its line height in normalized-fitted units. */
#define ELEMENT_TEXT_COMPONENT_SIZE 1.0f


// Static functions.
static Error CopyVariantName(const unsigned char* variant, unsigned char* outName)
{
    if ((variant == NULL) || (variant[0] == 0U))
    {
        outName[0] = 0U;
        return Error_CreateSuccess();
    }
    size_t Length = StringUTF8_GetByteLength(variant);
    if ((Length >= UI_THEME_VARIANT_NAME_CAPACITY) || !NamespacedKey_IsValidLocalName(variant))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "UI element: a variant's name is 1 to 31 of a-z, 0-9, _, . and -, starting with a letter.");
    }
    Memory_Copy(variant, outName, Length + 1U);
    return Error_CreateSuccess();
}

/* Whether the pointer at a screen position is over the widget: its top-most widget there is it or lies inside it. */
static bool IsPointerOver(Widget* widget, Vector2 screenPosition)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    Widget* Top = NULL;
    if (!UIScreen_IsPointerPresent(Screen) || !UIScreen_TryGetTopmostAt(Screen, screenPosition, &Top))
    {
        return false;
    }
    for (Widget* Current = Top; Current != NULL; Current = Widget_GetParent(Current))
    {
        if (Current == widget)
        {
            return true;
        }
    }
    return false;
}

static bool IsActivationKey(const UIScreen* screen, GameKey key)
{
    return (key == GameKey_Space) || UIElement_IsActionKey(screen, GameAction_UIAccept, key);
}


// Public functions: state.
UIElementState UIElementState_Choose(bool isEnabled, bool isPressed, bool isInvalid, bool isEntered, bool isSelected, bool isHovered,
    bool isFocused)
{
    if (!isEnabled)
    {
        return UIElementState_Disabled;
    }
    if (isPressed)
    {
        return UIElementState_Pressed;
    }
    if (isInvalid)
    {
        return UIElementState_Invalid;
    }
    if (isEntered)
    {
        return UIElementState_Entered;
    }
    if (isSelected)
    {
        return UIElementState_Selected;
    }
    if (isHovered)
    {
        return UIElementState_Hovered;
    }
    return isFocused ? UIElementState_Focused : UIElementState_Normal;
}


// Public functions: the look.
Error UIElementLook_Construct(UIElementLook* self, UIElementKind kind, const unsigned char* variant)
{
    Memory_Zero(self, sizeof(*self));
    self->_kind = kind;
    self->_state = UIElementState_Normal;
    self->_weights[UIElementState_Normal] = 1.0f;
    return CopyVariantName(variant, self->_variant);
}

Error UIElementLook_SetVariant(UIElementLook* self, const unsigned char* variant)
{
    unsigned char Name[UI_THEME_VARIANT_NAME_CAPACITY];
    Error Result = CopyVariantName(variant, Name);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Memory_Copy(Name, self->_variant, sizeof(Name));
    self->_hasCheckedTheme = false;
    return Error_CreateSuccess();
}

/* Logs each part this look's style leaves out, once per theme (the screen remembers), the first time this look sees it. */
static void ReportMissingParts(UIElementLook* self, UIScreen* screen)
{
    uint64_t Revision = UIScreen_GetThemeRevision(screen);
    if (self->_hasCheckedTheme && (self->_checkedThemeRevision == Revision))
    {
        return;
    }
    self->_hasCheckedTheme = true;
    self->_checkedThemeRevision = Revision;

    const UIThemeStyle* Style = UIElementLook_GetStyle(self, screen);
    size_t Count = UIElementKind_GetPartCount(self->_kind);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        const UIThemePartDescriptor* Part = UIElementKind_GetPart(self->_kind, (int32_t)Index);
        bool IsMissing = ((Part->Kind == UIThemePartKind_Box) && (UIThemeStyle_GetBox(Style, (int32_t)Index) == NULL))
            || ((Part->Kind == UIThemePartKind_Colors) && (UIThemeStyle_GetColors(Style, (int32_t)Index) == NULL));
        if (IsMissing)
        {
            UIScreen_ReportMissingThemePart(screen, UIElementKind_GetName(self->_kind), (size_t)self->_kind, Part->Key, Index);
        }
    }
}

void UIElementLook_Update(UIElementLook* self, UIScreen* screen, UIElementState state, double passedSeconds)
{
    ReportMissingParts(self, screen);

    float Transition = UITheme_GetTransitionSeconds(UIScreen_GetTheme(screen));
    self->_state = state;
    if (!self->_hasState || !(Transition > 0.0f))
    {
        self->_hasState = true;
        Memory_Zero(self->_weights, sizeof(self->_weights));
        self->_weights[state] = 1.0f;
        return;
    }
    if (self->_weights[state] >= 1.0f)
    {
        return;
    }
    // The others fade out at one rate and the target takes what they give up, so a state change that turns around
    // half way never jumps, and the weights always sum to 1.
    float Fade = (float)passedSeconds / Transition;
    float Others = 0.0f;
    for (int32_t Index = 0; Index < (int32_t)UIElementState_Count; Index++)
    {
        if (Index != (int32_t)state)
        {
            self->_weights[Index] = Math_MaxFloat(0.0f, self->_weights[Index] - Fade);
            Others += self->_weights[Index];
        }
    }
    self->_weights[state] = 1.0f - Others;
}

const UIThemeStyle* UIElementLook_GetStyle(const UIElementLook* self, const UIScreen* screen)
{
    return UITheme_FindStyle(UIScreen_GetTheme(screen), self->_kind, self->_variant);
}

Color UIElementLook_BlendColors(const UIElementLook* self, const UIStateColors* colors, Color fallback)
{
    if (colors == NULL)
    {
        return fallback;
    }
    if (self->_weights[self->_state] >= 1.0f)
    {
        return UIStateColors_Resolve(colors, self->_state, fallback);
    }
    float Channels[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int32_t Index = 0; Index < (int32_t)UIElementState_Count; Index++)
    {
        float Weight = self->_weights[Index];
        if (Weight > 0.0f)
        {
            Color Part = UIStateColors_Resolve(colors, (UIElementState)Index, fallback);
            Channels[0] += Weight * (float)Part.r;
            Channels[1] += Weight * (float)Part.g;
            Channels[2] += Weight * (float)Part.b;
            Channels[3] += Weight * (float)Part.a;
        }
    }
    return (Color)
    {
        .r = (unsigned char)(Math_ClampFloat(Channels[0], 0.0f, CHANNEL_MAX) + 0.5f),
        .g = (unsigned char)(Math_ClampFloat(Channels[1], 0.0f, CHANNEL_MAX) + 0.5f),
        .b = (unsigned char)(Math_ClampFloat(Channels[2], 0.0f, CHANNEL_MAX) + 0.5f),
        .a = (unsigned char)(Math_ClampFloat(Channels[3], 0.0f, CHANNEL_MAX) + 0.5f)
    };
}

Error UIElementLook_DrawBox(const UIElementLook* self, const UIThemeStyle* style, int32_t part, UIRenderContext* context,
    Rectangle box, float scale)
{
    const UIBoxStyle* Box = UIThemeStyle_GetBox(style, part);
    if (Box == NULL)
    {
        return UIBoxStyle_DrawFallback(context, box, scale);
    }
    UIBoxPaint Paint = UIBoxStyle_Resolve(Box, self->_state);
    Paint.Fill = UIElementLook_BlendColors(self, &Box->Colors, WHITE);
    Paint.Outline = UIElementLook_BlendColors(self, &Box->OutlineColors, WHITE);
    return UIBoxStyle_Draw(Box, context, box, &Paint, scale);
}


// Public functions: the text.
Error UIElementText_Construct(UIElementText* self, TextComponentFactory* factory, AssetManager* assets, Logger* logger,
    const unsigned char* text)
{
    Error Result = TextLayout_Construct(&self->_layout);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Result = TextDocument_Construct(&self->_document, factory, assets, logger);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = TextLayout_Deconstruct(&self->_layout);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    StringComponent* String = NULL;
    Result = TextComponentFactory_CreateString(factory, (text != NULL) ? text : (const unsigned char*)u8"", &String);
    if (Result.Code == ErrorCode_Success)
    {
        Result = StringComponent_SetSize(String, ELEMENT_TEXT_COMPONENT_SIZE);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = TextDocument_SetRoot(&self->_document, StringComponent_AsComponent(String));
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (String != NULL)
        {
            Error Cleanup = TextComponentFactory_ReturnTree(factory, StringComponent_AsComponent(String));
            Error_Deconstruct(&Cleanup);
        }
        Error Cleanup = TextDocument_Deconstruct(&self->_document);
        Error_Deconstruct(&Cleanup);
        Cleanup = TextLayout_Deconstruct(&self->_layout);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    self->_hasAppliedTheme = false;
    return Error_CreateSuccess();
}

Error UIElementText_Deconstruct(UIElementText* self)
{
    Error First = TextLayout_Deconstruct(&self->_layout);
    Error DocumentResult = TextDocument_Deconstruct(&self->_document);
    if ((DocumentResult.Code != ErrorCode_Success) && (First.Code == ErrorCode_Success))
    {
        First = DocumentResult;
    }
    else
    {
        Error_Deconstruct(&DocumentResult);
    }
    return First;
}

Error UIElementText_Set(UIElementText* self, Widget* widget, const unsigned char* text)
{
    if (text == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIElementText_Set: text must not be NULL.");
    }
    StringComponent* String = TextComponent_AsStringComponent(TextDocument_GetRoot(&self->_document));
    bool IsSame = false;
    Error Compare = StringUTF8_EqualsExact(StringComponent_GetText(String), text, &IsSame);
    Error_Deconstruct(&Compare);
    if (IsSame)
    {
        return Error_CreateSuccess();
    }
    Error Result = StringComponent_SetText(String, text);
    if (Result.Code == ErrorCode_Success)
    {
        Widget_InvalidateMeasure(widget);
    }
    return Result;
}

const unsigned char* UIElementText_Get(const UIElementText* self)
{
    return StringComponent_GetText(TextComponent_AsStringComponent(TextDocument_GetRoot(&self->_document)));
}

Error UIElementText_Refresh(UIElementText* self, const UIScreen* screen)
{
    const UITheme* Theme = UIScreen_GetTheme(screen);
    uint64_t Revision = UIScreen_GetThemeRevision(screen);
    if (!self->_hasAppliedTheme || (self->_appliedThemeRevision != Revision))
    {
        Error Result = TextDocument_SetDefaultFontName(&self->_document, UITheme_GetFontName(Theme));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        self->_hasAppliedTheme = true;
        self->_appliedThemeRevision = Revision;
    }
    TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
    Options.SizeMultiplier = Math_ClampFloat(UIScreen_PixelsToFitted(screen, UITheme_GetTextSize(Theme) * UIScreen_GetScale(screen)),
        0.0f, TEXT_LAYOUT_SIZE_MULTIPLIER_MAX);
    return TextLayout_Refresh(&self->_layout, &self->_document, &Options);
}

Vector2 UIElementText_GetPixelSize(const UIElementText* self, const UIScreen* screen)
{
    Vector2 Fitted = TextLayout_GetSize(&self->_layout);
    float Pixels = 1.0f / UIScreen_PixelsToFitted(screen, 1.0f);
    return (Vector2){ .x = Fitted.x * Pixels, .y = Fitted.y * Pixels };
}

Error UIElementText_Draw(const UIElementText* self, UIRenderContext* context, float x, float y, Color color)
{
    TextLayoutDrawArguments Arguments = TextLayoutDrawArguments_Create(RenderVector2D_Window((Vector2){ .x = x, .y = y }));
    Arguments.Tint = UIRenderColor_Multiply(UIRenderContext_GetTint(context), UIRenderColor_FromColor(color));
    return TextLayout_Draw(&self->_layout, UIRenderContext_GetRenderContext(context), &Arguments);
}


// Public functions: the press behaviour.
void UIPressable_OnMouseInput(UIPressable* self, Widget* widget, const WidgetMouseInputArgs* args, bool isEnabled,
    bool* outIsActivated)
{
    *outIsActivated = false;
    uint8_t Bit = UI_BUTTON_BIT(args->Button);
    bool IsActivating = ((self->_activationButtons & Bit) != 0U) && isEnabled;
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            if (IsActivating)
            {
                self->_heldButtons |= Bit;
                self->_isPointerInside = true;
                *outIsActivated = self->_clickMode == UIClickMode_OnPress;
            }
            return;
        case UIMouseInputType_Move:
            self->_isPointerInside = IsPointerOver(widget, args->ScreenPosition);
            return;
        case UIMouseInputType_ButtonRelease:
        {
            // OnRelease asks nothing of where the press began, so a press handed to it counts. What the press held ends here, whatever changed since.
            bool IsAsked = (self->_clickMode == UIClickMode_OnRelease)
                || ((self->_clickMode == UIClickMode_OnPressAndRelease) && ((self->_heldButtons & Bit) != 0U));
            *outIsActivated = IsActivating && IsAsked && IsPointerOver(widget, args->ScreenPosition);
            UIPressable_OnPressLost(self, args->Button);
            return;
        }
        case UIMouseInputType_ReleaseOver:
            *outIsActivated = IsActivating && (self->_clickMode == UIClickMode_OnRelease);
            return;
    }
}

void UIPressable_OnPressLost(UIPressable* self, GameKey button)
{
    self->_heldButtons &= (uint8_t)~UI_BUTTON_BIT(button);
}

void UIPressable_OnKeyboardInput(UIPressable* self, Widget* widget, const WidgetKeyboardInputArgs* args, bool isEnabled,
    bool* outIsHandled, bool* outIsActivated)
{
    *outIsActivated = false;
    switch (args->Type)
    {
        case UIKeyboardInputType_KeyPress:
            if (isEnabled && (self->_heldKey == GameKey_Unknown) && IsActivationKey(Widget_GetScreen(widget), args->Key))
            {
                self->_heldKey = args->Key;
                *outIsHandled = true;
                *outIsActivated = true;
            }
            return;
        case UIKeyboardInputType_KeyRepeat:
            *outIsHandled = (args->Key == self->_heldKey);
            return;
        case UIKeyboardInputType_KeyRelease:
            if (args->Key == self->_heldKey)
            {
                self->_heldKey = GameKey_Unknown;
                *outIsHandled = true;
            }
            return;
        case UIKeyboardInputType_Text:
            *outIsHandled = (self->_heldKey == GameKey_Space) && (args->Codepoint == (uint32_t)' ');
            return;
    }
}

void UIPressable_Update(UIPressable* self, const UIScreen* screen)
{
    const GameInput* Input = UIScreen_GetInput(screen);
    if (Input == NULL)
    {
        return;
    }
    if ((self->_heldKey != GameKey_Unknown) && ((GameInput_GetRawKeyState(Input, self->_heldKey) & GAME_INPUT_KEY_STATE_DOWN) == 0U))
    {
        self->_heldKey = GameKey_Unknown;
    }
    // A button that is up holds nothing, also in a widget type that does not answer OnPressLost.
    for (int32_t Button = (int32_t)GameKey_MouseLeft; (self->_heldButtons != 0U) && (Button <= (int32_t)GameKey_MouseBack); Button++)
    {
        if ((GameInput_GetRawKeyState(Input, (GameKey)Button) & GAME_INPUT_KEY_STATE_DOWN) == 0U)
        {
            UIPressable_OnPressLost(self, (GameKey)Button);
        }
    }
}

void UIPressable_Cancel(UIPressable* self)
{
    self->_heldButtons = 0U;
    self->_isPointerInside = false;
    self->_heldKey = GameKey_Unknown;
}


// Public functions: shared helpers.
bool UIElement_IsActionKey(const UIScreen* screen, GameActionId action, GameKey key)
{
    const GameInput* Input = UIScreen_GetInput(screen);
    if (Input == NULL)
    {
        return false;
    }
    size_t Count = GameInput_GetBindingCount(Input, action);
    for (size_t Index = 0U; Index < Count; Index++)
    {
        GameInputBinding Binding;
        if (GameInput_TryGetBinding(Input, action, Index, &Binding) && (Binding.Key == key))
        {
            return true;
        }
    }
    return false;
}

bool UIElement_IsActionPress(const UIScreen* screen, GameActionId action, GameKey key)
{
    const GameInput* Input = UIScreen_GetInput(screen);
    if (Input == NULL)
    {
        return false;
    }
    GameKey Keys[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    size_t KeyCount = GameInput_GetActionKeys(Input, action, Keys);
    for (size_t Index = 0U; Index < KeyCount; Index++)
    {
        if (Keys[Index] == key)
        {
            return true;
        }
    }
    return false;
}

/* The mouse data of a mouse input. */
static UIEventMouse MakeEventMouse(const WidgetMouseInputArgs* args, bool isOverWidget)
{
    return (UIEventMouse){ .Button = args->Button, .Modifiers = args->Modifiers, .ScreenPosition = args->ScreenPosition,
        .ScrollDelta = { .x = 0.0f, .y = 0.0f }, .IsOverWidget = isOverWidget };
}

/* The mouse data of what a key did: the middle of the widget. */
static UIEventMouse MakeEventMouseFromKey(Widget* widget, const WidgetKeyboardInputArgs* args)
{
    return (UIEventMouse){ .Button = args->Key, .Modifiers = args->Modifiers,
        .ScreenPosition = UIScreen_WidgetToScreen(Widget_GetScreen(widget), widget, (Vector2){ .x = 0.5f, .y = 0.5f }),
        .ScrollDelta = { .x = 0.0f, .y = 0.0f }, .IsOverWidget = false };
}

Error UIElement_PostMouseEvents(Widget* widget, const WidgetMouseInputArgs* args)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t Id = Widget_GetId(widget);
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
        {
            UIEventMouse Mouse = MakeEventMouse(args, true);
            Error Result = UIScreen_PostWidgetMouseEvent(Screen, Id, UIElementEvent_Pressed, &Mouse);
            if ((Result.Code != ErrorCode_Success) || (args->ClickCount != 2U))
            {
                return Result;
            }
            return UIScreen_PostWidgetMouseEvent(Screen, Id, UIElementEvent_DoubleClicked, &Mouse);
        }
        case UIMouseInputType_ButtonRelease:
        {
            bool IsOver = IsPointerOver(widget, args->ScreenPosition);
            UIEventMouse Mouse = MakeEventMouse(args, IsOver);
            Error Result = UIScreen_PostWidgetMouseEvent(Screen, Id, UIElementEvent_Released, &Mouse);
            if ((Result.Code != ErrorCode_Success) || (args->Button != GameKey_MouseRight) || !IsOver)
            {
                return Result;
            }
            return UIScreen_PostWidgetMouseEvent(Screen, Id, UIElementEvent_ContextRequested, &Mouse);
        }
        case UIMouseInputType_ReleaseOver:
        {
            UIEventMouse Mouse = MakeEventMouse(args, true);
            return UIScreen_PostWidgetMouseEvent(Screen, Id, UIElementEvent_Released, &Mouse);
        }
        case UIMouseInputType_Move:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

Error UIElement_PostActivatedByMouse(Widget* widget, const WidgetMouseInputArgs* args)
{
    UIEventMouse Mouse = MakeEventMouse(args, IsPointerOver(widget, args->ScreenPosition));
    return UIScreen_PostWidgetMouseEvent(Widget_GetScreen(widget), Widget_GetId(widget), UIElementEvent_Activated, &Mouse);
}

Error UIElement_PostActivatedByKey(Widget* widget, const WidgetKeyboardInputArgs* args)
{
    UIEventMouse Mouse = MakeEventMouseFromKey(widget, args);
    return UIScreen_PostWidgetMouseEvent(Widget_GetScreen(widget), Widget_GetId(widget), UIElementEvent_Activated, &Mouse);
}

Error UIElement_OnContextKey(Widget* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    if ((args->Type != UIKeyboardInputType_KeyPress) || !UIElement_IsActionPress(Screen, GameAction_UIContextMenu, args->Key))
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    UIEventMouse Mouse = MakeEventMouseFromKey(widget, args);
    return UIScreen_PostWidgetMouseEvent(Screen, Widget_GetId(widget), UIElementEvent_ContextRequested, &Mouse);
}

Error UIElement_ApplyOptions(Widget* widget, const UIElementOptions* options)
{
    Error Result = Widget_SetStackHint(widget, &options->Hint);
    if ((Result.Code == ErrorCode_Success) && options->HasPlacement)
    {
        Result = Widget_SetPlacement(widget, &options->Placement);
    }
    return Result;
}

Error UIElement_SetVariant(UIScreen* screen, uint64_t widgetId, const unsigned char* variant)
{
    Widget* Target = NULL;
    UIKit* Kit = UIKit_FromScreen(screen);
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIElement_SetVariant: the widget is gone, or its screen has no kit.");
    }
    UIElementLook* Look = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_Look));
    if (Look == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIElement_SetVariant: the widget is not an element with a look.");
    }
    Error Result = UIElementLook_SetVariant(Look, variant);
    if (Result.Code == ErrorCode_Success)
    {
        Widget_InvalidateMeasure(Target);
    }
    return Result;
}
