#include "UIPropertyBox.h"
#include <float.h>
#include "GameInput.h"
#include "UIContextMenu.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIMenu.h"
#include "UIScreen.h"
#include "UITextBoxInternal.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRString.h"


// Macros.
/** What a mixed number shows: a hyphen, which every UI font has. */
#define MIXED_TEXT ((const unsigned char*)u8"-")
/** Room for a shown number: a double at the most digits after the point fits. */
#define SHOWN_TEXT_CAPACITY ((size_t)400)


// Types.
/* The items of a string's context menu while it is not typed in. */
typedef enum StringMenuItemEnum
{
    StringMenuItem_Reset = 1,
    StringMenuItem_Copy,
    StringMenuItem_Paste
} StringMenuItem;


// Static functions: small helpers.
static UIPropertyBoxLookBase* GetBase(void* widget)
{
    return widget;
}

static bool HasModifier(uint8_t modifiers, uint8_t flag)
{
    return (modifiers & flag) != 0U;
}

static bool IsEnabled(const UIPropertyBoxLookBase* base)
{
    return base->TextBox._isEnabled;
}

static bool IsTyping(const UIPropertyBoxLookBase* base)
{
    return base->TextBox._isVisiting;
}

static uint64_t GetWidgetIdFrom(const UserData* userData)
{
    uint64_t Id = WIDGET_ID_INVALID;
    Memory_Copy(userData->Bytes, &Id, sizeof(Id));
    return Id;
}

static UserData MakeData(uint64_t widgetId)
{
    UserData Data = UserData_CreateEmpty();
    Memory_Copy(&widgetId, Data.Bytes, sizeof(widgetId));
    return Data;
}

static Error Resolve(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIPropertyBoxLookBase** outBase)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIPropertyBox: the widget is gone, or its screen has no kit.");
    }
    if (Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_PropertyBox)) == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIPropertyBox: the widget is not a property box.");
    }
    *outWidget = Target;
    // Every property box look's struct starts with UIPropertyBoxLookBase (UIKit_RegisterLook checks its size).
    *outBase = (UIPropertyBoxLookBase*)(void*)Target;
    return Error_CreateSuccess();
}

/* The text box a property box types in, for the text box's own menu (UITextBoxResolver). */
static Error ResolveText(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UITextBoxCapability** outBox)
{
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, outWidget, &Base);
    if (Result.Code == ErrorCode_Success)
    {
        *outBox = &Base->TextBox;
    }
    return Result;
}

/* The pointer's shape: a scrub's arrows over a number, an I-beam over a string and while typing. */
static void RefreshCursor(UIPropertyBoxLookBase* base)
{
    bool IsText = base->PropertyBox._isString || IsTyping(base);
    Widget_SetCursorShape(&base->Base, IsText ? UICursorShape_IBeam : UICursorShape_ResizeHorizontal);
}

/* What the box shows changed: a look measures it by that text, so its size is measured again. */
static void InvalidateMeasureIf(UIPropertyBoxLookBase* base, bool hasTextChanged)
{
    if (hasTextChanged)
    {
        Widget_InvalidateMeasure(&base->Base);
    }
}

/* Writes what shows while the box is not typed in, again only when what it shows from changed. */
static Error RefreshShownText(UIPropertyBoxLookBase* base)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    if (Self->_isString)
    {
        uint64_t Revision = UITextBoxCapability_GetContentRevision(&base->TextBox);
        if (Self->_hasShown && (Self->_shownTextRevision == Revision))
        {
            return Error_CreateSuccess();
        }
        bool HasTextChanged = false;
        Error Result = UITextContent_Set(&Self->_shownText, UITextEdit_GetText(&base->TextBox.Edit), &HasTextChanged);
        if (Result.Code == ErrorCode_Success)
        {
            Self->_hasShown = true;
            Self->_shownTextRevision = Revision;
            InvalidateMeasureIf(base, HasTextChanged);
        }
        return Result;
    }
    const UIValueSettings* Settings = UIValueCapability_GetSettings(&Self->Value);
    double Value = UIValueCapability_GetValue(&Self->Value);
    bool IsMixed = UIValueCapability_IsMixed(&Self->Value);
    bool IsCurrent = Self->_hasShown && (Self->_shownValue == Value) && (Self->_shownPrecision == Self->_precision)
        && (Self->_shownIsMixed == IsMixed) && (Self->_shownIsInteger == Settings->IsInteger);
    if (IsCurrent)
    {
        return Error_CreateSuccess();
    }
    unsigned char Bytes[SHOWN_TEXT_CAPACITY];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
    Error Result = Error_CreateSuccess();
    bool HasTextChanged = false;
    if (IsMixed)
    {
        Result = UITextContent_Set(&Self->_shownText, MIXED_TEXT, &HasTextChanged);
    }
    else
    {
        Result = UIValue_WriteFixedText(Value, Settings->IsInteger ? 0 : Self->_precision, &Text);
        if (Result.Code == ErrorCode_Success)
        {
            Result = UITextContent_Set(&Self->_shownText, Bytes, &HasTextChanged);
        }
    }
    if (Result.Code == ErrorCode_Success)
    {
        InvalidateMeasureIf(base, HasTextChanged);
        Self->_hasShown = true;
        Self->_shownValue = Value;
        Self->_shownPrecision = Self->_precision;
        Self->_shownIsMixed = IsMixed;
        Self->_shownIsInteger = Settings->IsInteger;
    }
    return Result;
}

/* Posts a string's whole change: Changed, then Committed, with its length. */
static Error PostStringChange(Widget* widget, const UIPropertyBoxLookBase* base)
{
    UIScreen* Screen = Widget_GetScreen(widget);
    UIPropertyValue Length = UIPropertyValue_FromDouble((double)UITextEdit_GetLength(&base->TextBox.Edit));
    Error Result = UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Changed, &Length);
    return (Result.Code == ErrorCode_Success) ? UIScreen_PostWidgetEvent(Screen, Widget_GetId(widget), UIElementEvent_Committed, &Length) : Result;
}

/* Reads the text being typed as a number. */
static bool TryReadTyped(const UIPropertyBoxLookBase* base, double* outValue)
{
    const UIPropertyBoxCapability* Self = &base->PropertyBox;
    return UIValue_ParseText(UITextEdit_GetText(&base->TextBox.Edit), UITextEdit_GetLength(&base->TextBox.Edit), UITextContent_Get(&Self->_unit),
        UIValueCapability_GetSettings(&Self->Value)->IsSinglePrecision, outValue);
}

/* Whether the text being typed is still the text typing began with. */
static bool IsTextAsBegun(const UIPropertyBoxLookBase* base)
{
    const GenericBuffer* Start = &base->PropertyBox._typingStart;
    size_t Length = UITextEdit_GetLength(&base->TextBox.Edit);
    return (Length == Start->_count) && ((Length == 0U) || Memory_IsEqual(UITextEdit_GetText(&base->TextBox.Edit), Start->_data, Length));
}


// Static functions: scrubbing.
/* A value clamped into the hard limits, a NaN or an infinity kept to the side it was going; with no limit on a side, the largest finite
 * double there. */
static double ClampToLimits(const UIValueSettings* settings, double value)
{
    double Lower = settings->HasMinimum ? settings->Minimum : -DBL_MAX;
    double Upper = settings->HasMaximum ? settings->Maximum : DBL_MAX;
    return (value > Lower) ? ((value < Upper) ? value : Upper) : Lower;
}

static double GetBoxWidthUIPixels(Widget* widget)
{
    float Scale = Widget_GetEffectiveScale(widget);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
    return (Scale > 0.0f) ? (double)(Pixels.width / Scale) : 0.0;
}

/* The press has gone far enough sideways: from here it scrubs, the cursor hidden, the keys its own (so Escape cancels it) though it is
 * not entered. */
static Error BeginScrub(UIPropertyBoxLookBase* base, Widget* widget)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    const UIValueSettings* Settings = UIValueCapability_GetSettings(&Self->Value);
    double Value = UIValueCapability_GetValue(&Self->Value);
    Self->_isScrubbing = true;
    Self->_scrubStartValue = Value;
    Self->_scrubHasRange = UIValueSettings_GetDragRangeAround(Settings, Value, &Self->_scrubMinimum, &Self->_scrubMaximum);
    Self->_scrubPlace = Self->_scrubHasRange ? UIValueSettings_ToShare(Settings, Self->_scrubMinimum, Self->_scrubMaximum, Value) : Value;
    UIValueCapability_BeginChange(&Self->Value);
    uint64_t Id = Widget_GetId(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    Error Result = Widget_SetNeedsEntering(widget, false);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Error Capture = UIScreen_CapturePointer(Screen, Id);
    Error_Deconstruct(&Capture); // Refused (another widget has it), the scrub goes on with the pointer's own moves.
    return Error_CreateSuccess();
}

static Error MoveScrub(UIPropertyBoxLookBase* base, Widget* widget, double deltaUIPixels, uint8_t modifiers)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    const UIValueSettings* Settings = UIValueCapability_GetSettings(&Self->Value);
    double Factor = HasModifier(modifiers, GAME_KEY_MODIFIER_SHIFT) ? Settings->FineFactor : 1.0;
    bool IsCoarse = HasModifier(modifiers, GAME_KEY_MODIFIER_CONTROL);
    double Value = 0.0;
    if (Self->_scrubHasRange)
    {
        double Width = GetBoxWidthUIPixels(widget);
        double Next = Self->_scrubPlace + ((Width > 0.0) ? ((deltaUIPixels / Width) * Factor) : 0.0);
        Self->_scrubPlace = (Next > 0.0) ? ((Next < 1.0) ? Next : 1.0) : 0.0;
        Value = UIValueSettings_FromShare(Settings, Self->_scrubMinimum, Self->_scrubMaximum, Self->_scrubPlace);
        if (IsCoarse)
        {
            Value = UIValueSettings_SnapCoarse(Settings, Self->_scrubMinimum, Self->_scrubMaximum, true, Value, Self->_scrubStartValue);
        }
    }
    else
    {
        double Next = Self->_scrubPlace + (deltaUIPixels * UIValueSettings_GetScrubRate(Settings, Self->_scrubStartValue) * Factor);
        // The place keeps to the limits as it goes, so turning back moves the value at once.
        Self->_scrubPlace = ClampToLimits(Settings, Next);
        Value = IsCoarse ? UIValueSettings_SnapCoarse(Settings, 0.0, 0.0, false, Self->_scrubPlace, Self->_scrubStartValue)
            : UIValueSettings_Normalize(Settings, Self->_scrubPlace);
    }
    return UIValueCapability_MoveTo(&Self->Value, widget, Value, NULL);
}

/* The scrub ends: committed at the release, taken back on a cancel; the keys go back to waiting for the box to be entered. */
static Error EndScrub(UIPropertyBoxLookBase* base, Widget* widget, bool isCancel)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    if (!Self->_isScrubbing)
    {
        return Error_CreateSuccess();
    }
    Self->_isScrubbing = false;
    UIScreen* Screen = Widget_GetScreen(widget);
    uint64_t Id = Widget_GetId(widget);
    if (isCancel && UIScreen_IsPointerCaptured(Screen) && (Screen->_pointerCaptureWidgetId == Id))
    {
        UIScreen_ReleasePointerCapture(Screen);
    }
    Error Result = isCancel ? UIValueCapability_CancelChange(&Self->Value, widget) : UIValueCapability_EndChange(&Self->Value, widget);
    if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(Screen, Id, &widget))
    {
        return Result;
    }
    return Widget_SetNeedsEntering(widget, true);
}

static void ForgetPress(UIPropertyBoxCapability* self)
{
    self->_isPressed = false;
    self->_isPressCancelled = false;
}


// Static functions: typing.
/* Enters the box by code: it hears OnEnter, which begins typing with everything selected. The widget may be gone after it. */
static Error EnterByCode(Widget* widget)
{
    return UIScreen_EnterWidget(Widget_GetScreen(widget), Widget_GetId(widget));
}

static Error BeginTyping(UIPropertyBoxLookBase* base, Widget* widget, UIEnterCause cause)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    UITextBoxCapability* TextBox = &base->TextBox;
    if (TextBox->_isVisitSuspended)
    {
        return UITextBoxInternal_OnEnter(widget, cause); // Back from its own menu: the same visit goes on.
    }
    if (!Self->_isString)
    {
        // Several values type from an empty text: what is typed is the value for all of them, and leaving it empty changes nothing.
        unsigned char Bytes[UI_VALUE_MAX_TEXT_BYTES];
        GenericBuffer Text;
        GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
        Error Written = UIValueCapability_IsMixed(&Self->Value) ? Error_CreateSuccess()
            : UIValue_WriteShortestText(UIValueCapability_GetValue(&Self->Value), UIValueCapability_GetSettings(&Self->Value)->IsSinglePrecision, &Text);
        if (Written.Code != ErrorCode_Success)
        {
            return Written;
        }
        Error Result = UITextBoxInternal_SetText(TextBox, Bytes, Text._count);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    GenericBuffer_Clear(&Self->_typingStart);
    // AppendRangeBytes only reads its source; the API is just not declared const.
    if (!GenericBuffer_AppendRangeBytes(&Self->_typingStart, (unsigned char*)UITextEdit_GetText(&TextBox->Edit), UITextEdit_GetLength(&TextBox->Edit)))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIPropertyBox: the text typing began with could not be kept.");
    }
    Self->_isTextInvalid = false;
    Error Result = UITextBoxInternal_OnEnter(widget, cause);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UITextEdit_SelectAll(&TextBox->Edit);
    UITextBoxInternal_OnCaretMoved(TextBox);
    RefreshCursor(base);
    return Error_CreateSuccess();
}

/* The text box's host: a number posts nothing while it is typed in, a string posts a whole change made while not typed in. */
static Error Host_OnEdited(Widget* widget, const UITextEditChange* change, bool isWhole)
{
    UIPropertyBoxLookBase* Base = GetBase(widget);
    UIPropertyBoxCapability* Self = &Base->PropertyBox;
    if (!Self->_isString)
    {
        double Value = 0.0;
        Self->_isTextInvalid = IsTyping(Base) && !TryReadTyped(Base, &Value);
        return Error_CreateSuccess();
    }
    return (isWhole && change->HasChanged) ? PostStringChange(widget, Base) : Error_CreateSuccess();
}

/* A visit ends: a cancel has put the text back, and the value never moved; kept, a text still as it began changes nothing, a number read
 * from it becomes the value as a whole change, and text that is no number goes back. */
static Error Host_OnVisitEnded(Widget* widget, UILeaveReason reason, bool hasTextChanged)
{
    (void)hasTextChanged;
    UIPropertyBoxLookBase* Base = GetBase(widget);
    UIPropertyBoxCapability* Self = &Base->PropertyBox;
    Self->_isTextInvalid = false;
    Self->_isEnteredByClick = false;
    RefreshCursor(Base);
    if ((reason == UILeaveReason_Cancel) || IsTextAsBegun(Base))
    {
        return Error_CreateSuccess();
    }
    if (Self->_isString)
    {
        return PostStringChange(widget, Base);
    }
    double Value = 0.0;
    if (!TryReadTyped(Base, &Value))
    {
        return UIElement_PostCue(widget, UIElementCue_Refused);
    }
    return UIValueCapability_ApplyWholeChange(&Self->Value, widget, UIValueSettings_Normalize(UIValueCapability_GetSettings(&Self->Value), Value), NULL);
}

static const UITextBoxHost PropertyBoxTextHost =
{
    .OnEdited = Host_OnEdited,
    .OnVisitEnded = Host_OnVisitEnded
};


// Static functions: resetting, copying, pasting.
static Error ResetToDefault(UIPropertyBoxLookBase* base, Widget* widget)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    if (Self->_isString)
    {
        return base->TextBox._hasDefault ? UITextBoxInternal_ResetToDefault(widget, &base->TextBox) : Error_CreateSuccess();
    }
    const UIValueSettings* Settings = UIValueCapability_GetSettings(&Self->Value);
    if (!Settings->HasDefault)
    {
        return Error_CreateSuccess();
    }
    return UIValueCapability_ApplyWholeChange(&Self->Value, widget, UIValueSettings_Normalize(Settings, Settings->Default), NULL);
}

static bool HasDefault(const UIPropertyBoxLookBase* base)
{
    return base->PropertyBox._isString ? base->TextBox._hasDefault : UIValueCapability_GetSettings(&base->PropertyBox.Value)->HasDefault;
}

static Error Copy(UIPropertyBoxLookBase* base, Widget* widget)
{
    return base->PropertyBox._isString ? UITextBoxInternal_CopyAll(widget, &base->TextBox) : UIValueCapability_Copy(&base->PropertyBox.Value, widget);
}

static Error Paste(UIPropertyBoxLookBase* base, Widget* widget)
{
    if (base->PropertyBox._isString)
    {
        return UITextBoxInternal_PasteAll(widget, &base->TextBox);
    }
    return UIValueCapability_Paste(&base->PropertyBox.Value, widget, UITextContent_Get(&base->PropertyBox._unit), NULL);
}

/* What the box does with the short list while it is not typed in (focused, or under the pointer): reset, copy, paste, the menu key. */
static Error OnShortListKey(UIPropertyBoxLookBase* base, Widget* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    const UIScreen* Screen = Widget_GetScreen(widget);
    if (args->Type != UIKeyboardInputType_KeyPress)
    {
        return Error_CreateSuccess();
    }
    if (UIElement_IsActionPress(Screen, GameAction_UIReset, args->Key))
    {
        *outIsHandled = HasDefault(base);
        return *outIsHandled ? ResetToDefault(base, widget) : Error_CreateSuccess();
    }
    if (UIElement_IsActionPress(Screen, GameAction_UICopy, args->Key))
    {
        *outIsHandled = true;
        return Copy(base, widget);
    }
    if (UIElement_IsActionPress(Screen, GameAction_UIPaste, args->Key))
    {
        *outIsHandled = true;
        return Paste(base, widget);
    }
    return UIElement_OnContextKey(widget, args, outIsHandled); // The menu key never comes by hover: only the focused box hears it.
}


// Static functions: the context menus.
static Error OnStringMenuItem(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, GetWidgetIdFrom(userData), &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result); // The box went away while its menu was open.
        return Error_CreateSuccess();
    }
    if (!IsEnabled(Base) && (activation->ItemId != (uint32_t)StringMenuItem_Copy))
    {
        return Error_CreateSuccess();
    }
    switch ((StringMenuItem)activation->ItemId)
    {
        case StringMenuItem_Reset:
            return ResetToDefault(Base, Target);
        case StringMenuItem_Copy:
            return Copy(Base, Target);
        case StringMenuItem_Paste:
            return Paste(Base, Target);
    }
    return Error_CreateSuccess();
}

static UIMenuItem MakeItem(StringMenuItem id, const char* label, bool isEnabled, const unsigned char* shortcut)
{
    UIMenuItem Item = UIMenuItem_Create((uint32_t)id, (const unsigned char*)label);
    Item.IsEnabled = isEnabled;
    Item.Shortcut = ((shortcut != NULL) && (shortcut[0] != 0U)) ? shortcut : NULL;
    return Item;
}

static Error OpenStringMenu(UIScreen* screen, const UIContextMenuRequest* request, uint64_t widgetId, const UIPropertyBoxLookBase* base)
{
    bool IsOn = IsEnabled(base);
    unsigned char Shortcuts[3][UI_MENU_SHORTCUT_CAPACITY];
    UIMenu_WriteShortcut(screen, GameAction_UIReset, Shortcuts[0], UI_MENU_SHORTCUT_CAPACITY);
    UIMenu_WriteShortcut(screen, GameAction_UICopy, Shortcuts[1], UI_MENU_SHORTCUT_CAPACITY);
    UIMenu_WriteShortcut(screen, GameAction_UIPaste, Shortcuts[2], UI_MENU_SHORTCUT_CAPACITY);
    UIMenuItem Items[] =
    {
        MakeItem(StringMenuItem_Reset, "Reset", IsOn && base->TextBox._hasDefault, Shortcuts[0]),
        UIMenuItem_Separator(),
        MakeItem(StringMenuItem_Copy, "Copy", !base->TextBox._isMasked, Shortcuts[1]),
        MakeItem(StringMenuItem_Paste, "Paste", IsOn, Shortcuts[2]),
    };
    UserData Data = MakeData(widgetId);
    return UIMenu_OpenAt(screen, request, Items, sizeof(Items) / sizeof(Items[0]), OnStringMenuItem, &Data);
}

/* Typed in: the text box's menu; else Reset, Copy and Paste (a number's through the value piece). A right press that cancelled a scrub
 * opens nothing. */
static Error ProvideMenu(UIScreen* screen, const UIContextMenuRequest* request, const UserData* userData)
{
    uint64_t Id = GetWidgetIdFrom(userData);
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, Id, &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    if (Base->PropertyBox._isMenuSuppressed)
    {
        // The right press that took a press back opens nothing when it is let go on the box; let go off it, no request comes, and the
        // flag must not swallow the menu key's.
        Base->PropertyBox._isMenuSuppressed = false;
        if (!request->IsFromKey)
        {
            return Error_CreateSuccess();
        }
    }
    if (IsTyping(Base))
    {
        return UITextBoxInternal_OpenContextMenu(screen, request, Id, ResolveText);
    }
    return Base->PropertyBox._isString ? OpenStringMenu(screen, request, Id, Base) : UIValue_OpenContextMenu(screen, request, Id);
}


// Static functions: input while not typed in.
static Error OnPressWhileShowing(UIPropertyBoxLookBase* base, Widget* widget, const WidgetMouseInputArgs* args)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    if (args->Button == GameKey_MouseRight)
    {
        Self->_isMenuSuppressed = false;
        if (Self->_isPressed && !Self->_isPressCancelled)
        {
            // A right press during a left one takes it back (its scrub too, once it began) and opens no menu.
            Self->_isPressCancelled = true;
            Self->_isMenuSuppressed = true;
            return EndScrub(base, widget, true);
        }
        return Error_CreateSuccess();
    }
    if (args->Button == GameKey_MouseMiddle)
    {
        Self->_isMiddlePressed = !Self->_isPressed;
        return Error_CreateSuccess();
    }
    if ((args->Button != GameKey_MouseLeft) || Self->_isPressed)
    {
        return Error_CreateSuccess();
    }
    if (Self->_isString)
    {
        // A string has nothing to scrub: a press types at once, everything selected.
        Self->_isEnteredByClick = true;
        return EnterByCode(widget);
    }
    Self->_isPressed = true;
    Self->_isPressCancelled = false;
    return Error_CreateSuccess();
}

static Error OnMoveWhileShowing(UIPropertyBoxLookBase* base, Widget* widget, const WidgetMouseInputArgs* args)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    if ((args->Button != GameKey_MouseLeft) || !Self->_isPressed || Self->_isPressCancelled)
    {
        return Error_CreateSuccess();
    }
    float Scale = Widget_GetEffectiveScale(widget);
    if (!Self->_isScrubbing)
    {
        Rectangle Pixels = UIScreen_GetWidgetPixelBox(Widget_GetScreen(widget), widget);
        float Sideways = (args->WidgetPosition.x - args->ClickStartWidgetPosition.x) * Pixels.width / Scale;
        if (Math_AbsFloat(Sideways) < UI_PROPERTY_BOX_SCRUB_THRESHOLD)
        {
            return Error_CreateSuccess();
        }
        return BeginScrub(base, widget);
    }
    return MoveScrub(base, widget, (double)(args->DeltaPixels.x / Scale), args->Modifiers);
}

static Error OnReleaseWhileShowing(UIPropertyBoxLookBase* base, Widget* widget, const WidgetMouseInputArgs* args)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    if ((args->Button == GameKey_MouseMiddle) && Self->_isMiddlePressed)
    {
        Self->_isMiddlePressed = false;
        bool IsOver = (args->WidgetPosition.x >= 0.0f) && (args->WidgetPosition.x <= 1.0f) && (args->WidgetPosition.y >= 0.0f)
            && (args->WidgetPosition.y <= 1.0f);
        return (IsOver && !Self->_isPressed) ? ResetToDefault(base, widget) : Error_CreateSuccess();
    }
    if ((args->Button != GameKey_MouseLeft) || !Self->_isPressed)
    {
        return Error_CreateSuccess();
    }
    bool WasScrubbing = Self->_isScrubbing;
    bool WasCancelled = Self->_isPressCancelled;
    ForgetPress(Self);
    if (WasScrubbing)
    {
        return EndScrub(base, widget, false);
    }
    if (WasCancelled)
    {
        return Error_CreateSuccess();
    }
    // A click without moving: the box is typed in, everything selected.
    Self->_isEnteredByClick = true;
    return EnterByCode(widget);
}


// Static functions: the kind's behaviour, for the kit's composed widget types.
static void* ResolveCapability(void* widget)
{
    return &GetBase(widget)->PropertyBox;
}

static void* ResolveValue(void* widget)
{
    UIPropertyBoxCapability* Self = &GetBase(widget)->PropertyBox;
    return Self->_isString ? NULL : &Self->Value;
}

/* Code changed the value or a setting: a scrub goes on from the new value. */
static Error Value_OnChangedByCode(Widget* widget)
{
    UIPropertyBoxLookBase* Base = GetBase(widget);
    UIPropertyBoxCapability* Self = &Base->PropertyBox;
    if (Self->_isScrubbing)
    {
        const UIValueSettings* Settings = UIValueCapability_GetSettings(&Self->Value);
        double Value = UIValueCapability_GetValue(&Self->Value);
        Self->_scrubHasRange = UIValueSettings_GetDragRangeAround(Settings, Value, &Self->_scrubMinimum, &Self->_scrubMaximum);
        Self->_scrubPlace = Self->_scrubHasRange ? UIValueSettings_ToShare(Settings, Self->_scrubMinimum, Self->_scrubMaximum, Value) : Value;
    }
    return RefreshShownText(Base);
}

/* Enabled or disabled, a number and a string alike: disabled, a scrub is taken back and a visit kept, and it takes no focus. */
static Error ApplyEnabled(Widget* widget, bool isEnabled)
{
    UIPropertyBoxLookBase* Base = GetBase(widget);
    UIPropertyBoxCapability* Self = &Base->PropertyBox;
    UIValueCapability_SetStatesQuietly(&Self->Value, isEnabled, UIValueCapability_IsMixed(&Self->Value));
    Base->TextBox._isEnabled = isEnabled;
    uint64_t Id = Widget_GetId(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    if (!isEnabled)
    {
        Self->_isMiddlePressed = false;
        if (Self->_isScrubbing)
        {
            Error Result = EndScrub(Base, widget, true);
            if ((Result.Code != ErrorCode_Success) || !UIScreen_TryGetWidgetById(Screen, Id, &widget))
            {
                return Result;
            }
        }
        ForgetPress(&GetBase(widget)->PropertyBox);
    }
    return Widget_SetIsFocusable(widget, isEnabled);
}

static Error Value_OnEnabledChanged(Widget* widget)
{
    return ApplyEnabled(widget, UIValueCapability_IsEnabled(&GetBase(widget)->PropertyBox.Value));
}

static const unsigned char* Value_GetUnit(Widget* widget)
{
    return UITextContent_Get(&GetBase(widget)->PropertyBox._unit);
}

static const UIValueHooks PropertyBoxValueHooks =
{
    .OnChangedByCode = Value_OnChangedByCode,
    .OnEnabledChanged = Value_OnEnabledChanged,
    .OnPartChanged = NULL,
    .GetUnit = Value_GetUnit
};

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIPropertyBox: the arguments are required.");
    }
    return UIPropertyBoxCapability_Construct(GetBase(widget), args);
}

static Error Behaviour_Deconstruct(void* widget)
{
    return UIPropertyBoxCapability_Deconstruct(GetBase(widget));
}

/* The text box's update (the caret's blink), then what shows. A press whose release cannot reach the box (it went away for input
 * meanwhile) ends in OnPressLost, with its scrub's commit. */
static Error Behaviour_Update(void* widget, ProgramTime time)
{
    Error Result = UITextBoxInternal_Update(widget, time);
    return (Result.Code == ErrorCode_Success) ? RefreshShownText(GetBase(widget)) : Result;
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    UIPropertyBoxLookBase* Base = GetBase(widget);
    UIPropertyBoxCapability* Self = &Base->PropertyBox;
    if (IsTyping(Base) && Widget_IsEntered(widget))
    {
        if ((args->Type == UIMouseInputType_ButtonPress) && (args->Button == GameKey_MouseLeft) && (args->ClickCount == 2U)
            && Self->_isEnteredByClick)
        {
            // The double click whose first click entered the box: everything stays selected, as entering selected it.
            Self->_isEnteredByClick = false;
            Error Result = UIElement_PostMouseCues(widget, args, IsEnabled(Base));
            Result = (Result.Code == ErrorCode_Success) ? UIElement_PostMouseEvents(widget, args) : Result;
            UITextEdit_SelectAll(&Base->TextBox.Edit);
            UITextBoxInternal_OnCaretMoved(&Base->TextBox);
            return Result;
        }
        if (args->Type == UIMouseInputType_ButtonPress)
        {
            Self->_isEnteredByClick = false;
        }
        return UITextBoxInternal_OnMouseInput(widget, args);
    }
    Error Result = UIElement_PostMouseCues(widget, args, IsEnabled(Base));
    if ((Result.Code != ErrorCode_Success) || !IsEnabled(Base))
    {
        return Result;
    }
    Result = UIElement_PostMouseEvents(widget, args);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            return OnPressWhileShowing(Base, widget, args);
        case UIMouseInputType_Move:
            return OnMoveWhileShowing(Base, widget, args);
        case UIMouseInputType_ButtonRelease:
            return OnReleaseWhileShowing(Base, widget, args);
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    UIPropertyBoxLookBase* Base = GetBase(widget);
    UIPropertyBoxCapability* Self = &Base->PropertyBox;
    if (button == GameKey_MouseMiddle)
    {
        Self->_isMiddlePressed = false;
    }
    if ((button == GameKey_MouseLeft) && Self->_isPressed)
    {
        bool WasScrubbing = Self->_isScrubbing;
        ForgetPress(Self);
        if (WasScrubbing)
        {
            return EndScrub(Base, widget, false);
        }
    }
    return UITextBoxInternal_OnPressLost(widget, button);
}

static Error Behaviour_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled)
{
    UIPropertyBoxLookBase* Base = GetBase(widget);
    UIPropertyBoxCapability* Self = &Base->PropertyBox;
    if (IsTyping(Base))
    {
        return UITextBoxInternal_OnScrollInput(widget, args, outIsHandled);
    }
    if (Self->_isString || !HasModifier(args->Modifiers, GAME_KEY_MODIFIER_CONTROL))
    {
        return Error_CreateSuccess(); // A plain wheel goes up the tree, so panels scroll.
    }
    *outIsHandled = true;
    if (!IsEnabled(Base) || Self->_isPressed || UIValueCapability_IsChanging(&Self->Value))
    {
        return Error_CreateSuccess();
    }
    double Target = UIValueSettings_Step(UIValueCapability_GetSettings(&Self->Value), UIValueCapability_GetValue(&Self->Value),
        (double)args->ScrollDelta.y, HasModifier(args->Modifiers, GAME_KEY_MODIFIER_SHIFT));
    return UIValueCapability_ApplyWholeChange(&Self->Value, widget, Target, NULL);
}

static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIPropertyBoxLookBase* Base = GetBase(widget);
    UIPropertyBoxCapability* Self = &Base->PropertyBox;
    if (Self->_isScrubbing)
    {
        // A scrub hears every key first (it switched entering off): Escape takes it back, and the rest does nothing (the modifiers it reads
        // are the held keys, which stay held; the game does not see them while the pointer is captured).
        *outIsHandled = true;
        if ((args->Type == UIKeyboardInputType_KeyPress) && UIElement_IsActionKey(Widget_GetScreen(widget), GameAction_UIBack, args->Key))
        {
            Self->_isPressCancelled = true;
            return EndScrub(Base, widget, true);
        }
        return Error_CreateSuccess();
    }
    if (!IsEnabled(Base))
    {
        return Error_CreateSuccess();
    }
    if (Widget_IsEntered(widget) && !args->IsHoverDelivery)
    {
        return UITextBoxInternal_OnKeyboardInput(widget, args, outIsHandled);
    }
    if (args->Type == UIKeyboardInputType_Text)
    {
        return Error_CreateSuccess();
    }
    return OnShortListKey(Base, widget, args, outIsHandled);
}

static Error Behaviour_OnEnter(void* widget, UIEnterCause cause)
{
    return BeginTyping(GetBase(widget), widget, cause);
}

static Error Behaviour_OnLeave(void* widget, UILeaveReason reason)
{
    return UITextBoxInternal_OnLeave(widget, reason);
}


// Fields.
static const UIKindCapability PropertyBoxCapabilities[] =
{
    { .Capability = UIElementCapability_PropertyBox, .Resolve = ResolveCapability },
    { .Capability = UIElementCapability_Value, .Resolve = ResolveValue }
};

static const UIKindBehaviour PropertyBoxBehaviour =
{
    .Kind = UIElementKind_PropertyBox,
    .Capabilities = PropertyBoxCapabilities,
    .CapabilityCount = sizeof(PropertyBoxCapabilities) / sizeof(PropertyBoxCapabilities[0]),
    .BaseSize = sizeof(UIPropertyBoxLookBase),
    .IsFocusable = true,
    .NeedsEntering = true,
    .EntersItself = true,
    .Construct = Behaviour_Construct,
    .Deconstruct = Behaviour_Deconstruct,
    .Update = Behaviour_Update,
    .Layout = UITextBoxInternal_Layout,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnScrollInput = Behaviour_OnScrollInput,
    .OnKeyboardInput = Behaviour_OnKeyboardInput,
    .OnEnter = Behaviour_OnEnter,
    .OnLeave = Behaviour_OnLeave
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIPropertyBox_GetKindBehaviour(void)
{
    return &PropertyBoxBehaviour;
}


// Public functions: helpers.
UIPropertyBoxOptions UIPropertyBoxOptions_Create(void)
{
    UIPropertyBoxOptions Options;
    Memory_Zero(&Options, sizeof(Options));
    Options.Element = UIElementOptions_Create();
    Options.IsString = false;
    Options.Value = UIValueOptions_Create();
    Options.Value.Settings = UIValueSettings_Create();
    Options.Precision = UI_PROPERTY_BOX_DEFAULT_PRECISION;
    Options.Unit = NULL;
    Options.ShowsFill = false;
    Options.Text = NULL;
    Options.Limits = UITextEditLimits_Create();
    Options.Limits.AllowsNewlines = false;
    Options.HasDefaultText = false;
    Options.DefaultText = NULL;
    return Options;
}

UIPropertyBoxOptions UIPropertyBoxOptions_CreateRange(double minimum, double maximum, double value)
{
    UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
    Options.Value = UIValueOptions_CreateRange(minimum, maximum, value);
    Options.ShowsFill = true;
    return Options;
}

UIPropertyBoxOptions UIPropertyBoxOptions_CreateString(const unsigned char* text)
{
    UIPropertyBoxOptions Options = UIPropertyBoxOptions_Create();
    Options.IsString = true;
    Options.Text = text;
    return Options;
}


// Public functions: behaviour helpers.
Error UIPropertyBoxCapability_Construct(UIPropertyBoxLookBase* base, const UIPropertyBoxArgs* args)
{
    const UIPropertyBoxOptions* Options = args->Options;
    Widget* Target = &base->Base;
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    if ((Options->Unit != NULL) && ((StringUTF8_GetByteLength(Options->Unit) > UI_PROPERTY_BOX_MAX_UNIT_BYTES) || !StringUTF8_IsEncodingValid(Options->Unit)))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIPropertyBox: the unit is longer than UI_PROPERTY_BOX_MAX_UNIT_BYTES or not valid UTF-8.");
    }
    UITextBoxOptions TextOptions = UITextBoxOptions_Create();
    TextOptions.Element = Options->Element;
    TextOptions.Text = Options->IsString ? Options->Text : NULL;
    TextOptions.Limits = Options->IsString ? Options->Limits : UITextEditLimits_Create();
    TextOptions.Limits.AllowsNewlines = false;
    if (!Options->IsString)
    {
        TextOptions.Limits.MaxBytes = UI_VALUE_MAX_TEXT_BYTES;
    }
    TextOptions.UndoScope = UITextBoxUndoScope_Visit;
    TextOptions.IsEnabled = Options->Value.IsEnabled;
    TextOptions.AllowsOverwrite = false;
    TextOptions.EscapeReverts = true;
    TextOptions.HasDefault = Options->IsString && Options->HasDefaultText;
    TextOptions.Default = Options->DefaultText;
    UITextBoxArgs TextArgs = { .Kit = args->Kit, .Options = &TextOptions };
    Error Result = UITextBoxCapability_Construct(&base->TextBox, Target, &TextArgs);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    // A string's value piece answers nothing (ResolveValue): it is made from the default options so it always has something to release.
    UIValueOptions ValueOptions = Options->IsString ? UIValueOptions_Create() : Options->Value;
    Result = UIValueCapability_Construct(&Self->Value, &ValueOptions, &PropertyBoxValueHooks, false);
    if (Result.Code != ErrorCode_Success)
    {
        Error Cleanup = UITextBoxCapability_Deconstruct(&base->TextBox);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    Result = UITextContent_Construct(&Self->_unit, Options->Unit);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UITextContent_Construct(&Self->_shownText, NULL);
        if (Result.Code != ErrorCode_Success)
        {
            UITextContent_Deconstruct(&Self->_unit);
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        UIValueCapability_Deconstruct(&Self->Value);
        Error Cleanup = UITextBoxCapability_Deconstruct(&base->TextBox);
        Error_Deconstruct(&Cleanup);
        return Result;
    }
    Self->_isString = Options->IsString;
    Self->_precision = Options->Precision; // Showing keeps it to [0, UI_VALUE_MAX_PRECISION] (UIValue_WriteFixedText).
    Self->_showsFill = Options->ShowsFill;
    Self->_hasShown = false;
    Self->_isPressed = false;
    Self->_isScrubbing = false;
    Self->_isPressCancelled = false;
    Self->_isMiddlePressed = false;
    Self->_isMenuSuppressed = false;
    Self->_isTextInvalid = false;
    Self->_isEnteredByClick = false;
    GenericBuffer_AllocateVariable(&Self->_typingStart, 0U, sizeof(unsigned char));
    UITextBoxCapability_SetHost(&base->TextBox, &PropertyBoxTextHost);
    RefreshCursor(base);
    // The widget is not alive yet: nothing below can fail or run a hook.
    Error Flags = Widget_SetIsFocusable(Target, Options->Value.IsEnabled);
    Error_Deconstruct(&Flags);
    return RefreshShownText(base);
}

Error UIPropertyBoxCapability_Deconstruct(UIPropertyBoxLookBase* base)
{
    UIPropertyBoxCapability* Self = &base->PropertyBox;
    UITextContent_Deconstruct(&Self->_shownText);
    UITextContent_Deconstruct(&Self->_unit);
    UIValueCapability_Deconstruct(&Self->Value);
    Memory_Free(Self->_typingStart._data);
    Memory_Zero(&Self->_typingStart, sizeof(Self->_typingStart));
    return UITextBoxCapability_Deconstruct(&base->TextBox);
}

UIElementState UIPropertyBoxCapability_GetState(const UIPropertyBoxLookBase* base)
{
    const UIPropertyBoxCapability* Self = &base->PropertyBox;
    bool IsInvalid = IsTyping(base) && Self->_isTextInvalid;
    bool IsEntered = Widget_IsEntered(&base->Base) || (IsTyping(base) && base->TextBox._isVisitSuspended);
    return UIElementState_Choose(IsEnabled(base), Self->_isPressed || Self->_isScrubbing, IsInvalid, IsEntered, false, Widget_IsHovered(&base->Base),
        Widget_IsFocused(&base->Base));
}

bool UIPropertyBoxCapability_IsTyping(const UIPropertyBoxLookBase* base)
{
    return IsTyping(base);
}

bool UIPropertyBoxCapability_ShowsUnit(const UIPropertyBoxCapability* self)
{
    return !self->_isString && !UIValueCapability_IsMixed(&self->Value) && (UITextContent_Get(&self->_unit)[0] != 0U);
}

bool UIPropertyBoxCapability_ShowsFill(const UIPropertyBoxCapability* self)
{
    return self->_showsFill && !self->_isString && !UIValueCapability_IsMixed(&self->Value) && UIValueCapability_HasDragRange(&self->Value);
}

double UIPropertyBoxCapability_GetFillShare(const UIPropertyBoxCapability* self)
{
    return UIValueCapability_GetShare(&self->Value);
}

Error UIPropertyBox_AttachContextMenu(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UserData Data = MakeData(widgetId);
    return UIContextMenu_SetProvider(screen, widgetId, ProvideMenu, &Data);
}


// Public functions: calling code.
Error UIPropertyBox_SetPrecision(UIScreen* screen, uint64_t widgetId, int32_t precision)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Base->PropertyBox._precision = precision;
    Widget_InvalidateMeasure(Target);
    return RefreshShownText(Base);
}

Error UIPropertyBox_SetUnit(UIScreen* screen, uint64_t widgetId, const unsigned char* unit)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((unit != NULL) && (StringUTF8_GetByteLength(unit) > UI_PROPERTY_BOX_MAX_UNIT_BYTES))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIPropertyBox_SetUnit: the unit is longer than UI_PROPERTY_BOX_MAX_UNIT_BYTES.");
    }
    if ((unit != NULL) && !StringUTF8_IsEncodingValid(unit))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "UIPropertyBox_SetUnit: the unit is not valid UTF-8.");
    }
    bool HasChanged = false;
    Result = UITextContent_Set(&Base->PropertyBox._unit, unit, &HasChanged);
    if ((Result.Code == ErrorCode_Success) && HasChanged)
    {
        Widget_InvalidateMeasure(Target);
    }
    return Result;
}

Error UIPropertyBox_SetShowsFill(UIScreen* screen, uint64_t widgetId, bool showsFill)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code == ErrorCode_Success)
    {
        Base->PropertyBox._showsFill = showsFill;
    }
    return Result;
}

Error UIPropertyBox_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    return (Result.Code == ErrorCode_Success) ? ApplyEnabled(Target, isEnabled) : Result;
}

Error UIPropertyBox_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Base->PropertyBox._isString)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIPropertyBox_SetText: the box holds a number (UIValue_Set sets it).");
    }
    const unsigned char* Text = (text != NULL) ? text : (const unsigned char*)u8"";
    if (!StringUTF8_IsEncodingValid(Text))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "UIPropertyBox_SetText: the text is not valid UTF-8.");
    }
    Result = UITextBoxInternal_SetText(&Base->TextBox, Text, StringUTF8_GetByteLength(Text));
    return (Result.Code == ErrorCode_Success) ? RefreshShownText(Base) : Result;
}

Error UIPropertyBox_GetText(UIScreen* screen, uint64_t widgetId, const unsigned char** outText, size_t* outLength)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsShown = Base->PropertyBox._isString || IsTyping(Base);
    *outText = IsShown ? UITextEdit_GetText(&Base->TextBox.Edit) : (const unsigned char*)u8"";
    if (outLength != NULL)
    {
        *outLength = IsShown ? UITextEdit_GetLength(&Base->TextBox.Edit) : 0U;
    }
    return Error_CreateSuccess();
}

Error UIPropertyBox_SetTextLimits(UIScreen* screen, uint64_t widgetId, const UITextEditLimits* limits)
{
    if (limits == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIPropertyBox_SetTextLimits: the limits must not be NULL.");
    }
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Base->PropertyBox._isString)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIPropertyBox_SetTextLimits: the box holds a number.");
    }
    UITextEditLimits Limits = *limits;
    Limits.AllowsNewlines = false;
    Result = UITextBoxInternal_SetLimits(&Base->TextBox, &Limits);
    return (Result.Code == ErrorCode_Success) ? RefreshShownText(Base) : Result;
}

Error UIPropertyBox_SetDefaultText(UIScreen* screen, uint64_t widgetId, bool hasDefault, const unsigned char* text)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!Base->PropertyBox._isString)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIPropertyBox_SetDefaultText: the box holds a number (UIValue_SetDefault sets its default).");
    }
    if ((text != NULL) && !StringUTF8_IsEncodingValid(text))
    {
        return Error_Construct2(ErrorCode_InvalidTextEncoding, "UIPropertyBox_SetDefaultText: the text is not valid UTF-8.");
    }
    Result = UITextContent_Set(&Base->TextBox._default, hasDefault ? text : NULL, NULL);
    if (Result.Code == ErrorCode_Success)
    {
        Base->TextBox._hasDefault = hasDefault;
    }
    return Result;
}

Error UIPropertyBox_IsTyping(UIScreen* screen, uint64_t widgetId, bool* outIsTyping)
{
    Widget* Target = NULL;
    UIPropertyBoxLookBase* Base = NULL;
    Error Result = Resolve(screen, widgetId, &Target, &Base);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsTyping = IsTyping(Base);
    }
    return Result;
}
