#include "UIElement.h"
#include "UIContextMenu.h"
#include "AssetManager.h"
#include "NamespacedKey.h"
#include "TextComponent.h"
#include "TextComponentFactory.h"
#include "UIKit.h"
#include "UIRenderContext.h"
#include "UIScreen.h"
#include "wr/WRChar.h"
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
    bool IsSame = false;
    Error Compared = StringUTF8_EqualsExact(Name, self->_variant, &IsSame); // Both are terminated names: it cannot fail.
    Error_Deconstruct(&Compared);
    if (IsSame)
    {
        return Error_CreateSuccess(); // The same variant: what it read from the theme still holds.
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


// Public functions: the text content.
/* A copy of a text, or an error for one that is not valid UTF-8. */
static Error CopyText(const unsigned char* text, unsigned char** outCopy)
{
    const unsigned char* Source = (text != NULL) ? text : (const unsigned char*)u8"";
    if (!StringUTF8_IsEncodingValid(Source))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "UI element: the text is not valid UTF-8.");
    }
    size_t Size = StringUTF8_GetByteLength(Source) + 1U;
    *outCopy = Memory_Allocate(Size);
    Memory_Copy(Source, *outCopy, Size);
    return Error_CreateSuccess();
}

Error UITextContent_Construct(UITextContent* self, const unsigned char* text)
{
    self->_text = NULL;
    self->_revision = 1U;
    return CopyText(text, &self->_text);
}

void UITextContent_Deconstruct(UITextContent* self)
{
    Memory_Free(self->_text);
    self->_text = NULL;
}

Error UITextContent_Set(UITextContent* self, const unsigned char* text, bool* outHasChanged)
{
    if (outHasChanged != NULL)
    {
        *outHasChanged = false;
    }
    if (text == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UITextContent_Set: text must not be NULL.");
    }
    bool IsSame = false;
    Error Compare = StringUTF8_EqualsExact(self->_text, text, &IsSame);
    Error_Deconstruct(&Compare);
    if (IsSame)
    {
        return Error_CreateSuccess();
    }
    unsigned char* Copy = NULL;
    Error Result = CopyText(text, &Copy);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Memory_Free(self->_text);
    self->_text = Copy;
    self->_revision++;
    if (outHasChanged != NULL)
    {
        *outHasChanged = true;
    }
    return Error_CreateSuccess();
}


// Public functions: the text a look draws.
Error UIElementText_Construct(UIElementText* self, TextComponentFactory* factory, AssetManager* assets, Logger* logger)
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
    Result = TextComponentFactory_CreateString(factory, (const unsigned char*)u8"", &String);
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
    self->_appliedThemeRevision = 0U;
    self->_hasAppliedContent = false;
    self->_appliedContentRevision = 0U;
    self->_isWrapped = false;
    self->_wrapWidth = 0.0f;
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

void UIElementText_SetWrapWidth(UIElementText* self, bool isWrapped, float wrapWidth)
{
    self->_isWrapped = isWrapped && (wrapWidth > 0.0f) && !Math_IsInfinityFloat(wrapWidth);
    self->_wrapWidth = self->_isWrapped ? wrapWidth : 0.0f;
}

Error UIElementText_Refresh(UIElementText* self, const Widget* widget, const UITextContent* content)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    if ((content != NULL) && (!self->_hasAppliedContent || (self->_appliedContentRevision != UITextContent_GetRevision(content))))
    {
        StringComponent* String = TextComponent_AsStringComponent(TextDocument_GetRoot(&self->_document));
        Error Result = StringComponent_SetText(String, UITextContent_Get(content));
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        self->_hasAppliedContent = true;
        self->_appliedContentRevision = UITextContent_GetRevision(content);
    }
    const UITheme* Theme = UIScreen_GetTheme(Screen);
    uint64_t Revision = UIScreen_GetThemeRevision(Screen);
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
    float Scale = Widget_GetEffectiveScale(widget);
    TextLayoutOptions Options = TextLayoutOptions_CreateDefault();
    Options.SizeMultiplier = Math_ClampFloat(UIScreen_PixelsToFitted(Screen, UITheme_GetTextSize(Theme) * Scale), 0.0f,
        TEXT_LAYOUT_SIZE_MULTIPLIER_MAX);
    if (self->_isWrapped)
    {
        Options.HasMaxWidth = true;
        Options.MaxWidth = UIScreen_PixelsToFitted(Screen, self->_wrapWidth * Scale);
        Options.IsWrapEnabled = true;
    }
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


// Public functions: the icon.
Error UIElementIcon_FromSpriteAnimation(const unsigned char* animationName, UIElementIcon* outIcon)
{
    if ((animationName == NULL) || (outIcon == NULL))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIElementIcon_FromSpriteAnimation: a name is required.");
    }
    UIElementIcon Icon = UIElementIcon_None();
    GenericBuffer Name;
    GenericBuffer_CreateConstant(&Name, Icon.Name, sizeof(Icon.Name), sizeof(unsigned char), 0U);
    Error Result = NamespacedKey_Parse(animationName, StringUTF8_GetByteLength(animationName), NULL, &Name);
    if ((Result.Code != ErrorCode_Success) || !AssetManager_IsValidAssetName(Icon.Name))
    {
        Error_Deconstruct(&Result);
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIElementIcon_FromSpriteAnimation: the name is not a valid asset name.");
    }
    Icon.Kind = UIElementIconKind_SpriteAnimation;
    *outIcon = Icon;
    return Error_CreateSuccess();
}

bool UIElementIcon_IsValid(const UIElementIcon* icon)
{
    switch (icon->Kind)
    {
        case UIElementIconKind_None:
            return true;
        case UIElementIconKind_Letter:
            return (icon->Letter != 0U) && CharUTF8_IsCodePointValid((CodePoint)icon->Letter);
        case UIElementIconKind_SpriteAnimation:
        {
            size_t Length = 0U;
            while ((Length < sizeof(icon->Name)) && (icon->Name[Length] != 0U))
            {
                Length++;
            }
            return (Length < sizeof(icon->Name)) && AssetManager_IsValidAssetName(icon->Name);
        }
    }
    return false;
}

bool UIElementIcon_Equals(const UIElementIcon* a, const UIElementIcon* b)
{
    if (a->Kind != b->Kind)
    {
        return false;
    }
    switch (a->Kind)
    {
        case UIElementIconKind_None:
            return true;
        case UIElementIconKind_Letter:
            return a->Letter == b->Letter;
        case UIElementIconKind_SpriteAnimation:
        {
            bool IsSame = false;
            Error Compare = StringUTF8_EqualsExact(a->Name, b->Name, &IsSame);
            Error_Deconstruct(&Compare);
            return IsSame;
        }
    }
    return false;
}

Error UIElementIconView_Construct(UIElementIconView* self, TextComponentFactory* factory, AssetManager* assets, Logger* logger)
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
    self->_factory = factory;
    self->_shown = UIElementIcon_None();
    self->_hasAppliedTheme = false;
    self->_appliedThemeRevision = 0U;
    return Error_CreateSuccess();
}

Error UIElementIconView_Deconstruct(UIElementIconView* self)
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

/* The tree an icon shows: a letter one line tall, or an animation in a square one line tall; NULL for none. */
static Error MakeIconTree(TextComponentFactory* factory, const UIElementIcon* icon, TextComponent** outRoot)
{
    *outRoot = NULL;
    switch (icon->Kind)
    {
        case UIElementIconKind_None:
            return Error_CreateSuccess();
        case UIElementIconKind_Letter:
        {
            StringComponent* Letter = NULL;
            Error Result = TextComponentFactory_CreateCodepoint(factory, (CodePoint)icon->Letter, &Letter);
            if (Result.Code == ErrorCode_Success)
            {
                *outRoot = StringComponent_AsComponent(Letter);
                Result = StringComponent_SetSize(Letter, ELEMENT_TEXT_COMPONENT_SIZE);
            }
            return Result;
        }
        case UIElementIconKind_SpriteAnimation:
        {
            SpriteComponent* Sprite = NULL;
            Error Result = TextComponentFactory_CreateSprite(factory, icon->Name, &Sprite);
            if (Result.Code == ErrorCode_Success)
            {
                *outRoot = SpriteComponent_AsComponent(Sprite);
                Result = SpriteComponent_SetSize(Sprite, (Vector2){ .x = ELEMENT_TEXT_COMPONENT_SIZE, .y = ELEMENT_TEXT_COMPONENT_SIZE });
            }
            return Result;
        }
    }
    return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIElementIconView: an icon of an unknown kind.");
}

Error UIElementIconView_Refresh(UIElementIconView* self, const Widget* widget, const UIElementIcon* icon)
{
    if (!UIElementIcon_Equals(&self->_shown, icon))
    {
        TextComponent* Root = NULL;
        Error Result = MakeIconTree(self->_factory, icon, &Root);
        if (Result.Code == ErrorCode_Success)
        {
            Result = TextDocument_SetRoot(&self->_document, Root);
            Root = NULL; // The document owns it now, even when returning the old tree failed.
        }
        if (Root != NULL)
        {
            Error Cleanup = TextComponentFactory_ReturnTree(self->_factory, Root);
            Error_Deconstruct(&Cleanup);
        }
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
        self->_shown = *icon;
    }
    const UIScreen* Screen = Widget_GetScreen(widget);
    const UITheme* Theme = UIScreen_GetTheme(Screen);
    uint64_t Revision = UIScreen_GetThemeRevision(Screen);
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
    Options.SizeMultiplier = Math_ClampFloat(UIScreen_PixelsToFitted(Screen, UITheme_GetTextSize(Theme) * Widget_GetEffectiveScale(widget)),
        0.0f, TEXT_LAYOUT_SIZE_MULTIPLIER_MAX);
    return TextLayout_Refresh(&self->_layout, &self->_document, &Options);
}

Error UIElementIconView_Update(UIElementIconView* self, ProgramTime time)
{
    return TextDocument_Update(&self->_document, time);
}

Vector2 UIElementIconView_GetPixelSize(const UIElementIconView* self, const UIScreen* screen)
{
    if (!UIElementIconView_IsShowing(self))
    {
        return (Vector2){ .x = 0.0f, .y = 0.0f };
    }
    Vector2 Fitted = TextLayout_GetSize(&self->_layout);
    float Pixels = 1.0f / UIScreen_PixelsToFitted(screen, 1.0f);
    return (Vector2){ .x = Fitted.x * Pixels, .y = Fitted.y * Pixels };
}

Error UIElementIconView_Draw(const UIElementIconView* self, UIRenderContext* context, float x, float y, Color color)
{
    if (!UIElementIconView_IsShowing(self))
    {
        return Error_CreateSuccess();
    }
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
void UIElement_DrawArrow(UIRenderContext* context, Rectangle box, UIArrowDirection direction, Color color)
{
    bool IsVertical = (direction == UIArrowDirection_Up) || (direction == UIArrowDirection_Down);
    float Base = Math_FloorFloat((IsVertical ? box.width : box.height) + 0.5f);
    float Room = Math_FloorFloat((IsVertical ? box.height : box.width) + 0.5f);
    if (!(Base >= 1.0f) || !(Room >= 1.0f))
    {
        return;
    }
    // A step per pixel row: the base row is Base wide and each row toward the point two pixels narrower.
    float Height = Math_MinFloat(Room, Math_CeilFloat(Base * 0.5f));
    float BaseStart = Math_FloorFloat((IsVertical ? box.x : box.y) + 0.5f);
    float PointStart = Math_FloorFloat((IsVertical ? box.y : box.x) + ((Room - Height) * 0.5f) + 0.5f);
    for (float Step = 0.0f; Step < Height; Step += 1.0f)
    {
        float Width = Math_MaxFloat(1.0f, Base - (2.0f * Step));
        float Across = BaseStart + Math_FloorFloat((Base - Width) * 0.5f);
        bool IsBaseFirst = (direction == UIArrowDirection_Down) || (direction == UIArrowDirection_Right);
        float Along = IsBaseFirst ? (PointStart + Step) : (PointStart + Height - 1.0f - Step);
        Rectangle Row = IsVertical ? (Rectangle){ .x = Across, .y = Along, .width = Width, .height = 1.0f }
            : (Rectangle){ .x = Along, .y = Across, .width = 1.0f, .height = Width };
        UIRenderContext_RenderPixelRectangle(context, Row, color);
    }
}

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

Error UIElement_PostCue(Widget* widget, UIElementCue cue)
{
    return UIScreen_PostCue(Widget_GetScreen(widget), Widget_GetId(widget), cue);
}

Error UIElement_PostMouseCues(Widget* widget, const WidgetMouseInputArgs* args, bool isEnabled)
{
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            return UIElement_PostCue(widget, isEnabled ? UIElementCue_Press : UIElementCue_Refused);
        case UIMouseInputType_ButtonRelease:
            return isEnabled ? UIElement_PostCue(widget, UIElementCue_Release) : Error_CreateSuccess();
        case UIMouseInputType_Move:
        case UIMouseInputType_ReleaseOver:
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
    Error Result = UIScreen_PostWidgetMouseEvent(Screen, Widget_GetId(widget), UIElementEvent_ContextRequested, &Mouse);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return UIContextMenu_Request(Screen, Widget_GetId(widget), Mouse.ScreenPosition, true, NULL);
}

Error UIElement_ApplyOptions(Widget* widget, const UIElementOptions* options)
{
    Error Result = Widget_SetStackHint(widget, &options->Hint);
    if ((Result.Code == ErrorCode_Success) && options->HasPlacement)
    {
        Result = Widget_SetPlacement(widget, &options->Placement);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = Widget_SetScale(widget, options->Scale);
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

Error UIElement_SetScale(UIScreen* screen, uint64_t widgetId, float scale)
{
    Widget* Target = NULL;
    if ((screen == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIElement_SetScale: the widget is gone.");
    }
    return Widget_SetScale(Target, scale);
}
