#include "UIValue.h"
#include <float.h>
#include "GameInput.h"
#include "UIContextMenu.h"
#include "UIElement.h"
#include "UIKit.h"
#include "UIMenu.h"
#include "UIScreen.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"
#include "wr/WRNumber.h"
#include "wr/WRString.h"


// Macros.
/** Room for a number's text while it is read: the longest it may be, and its NUL. */
#define PARSE_CAPACITY (UI_VALUE_MAX_TEXT_BYTES + 1U)
/** FLT_MAX and half its last place: a double below it rounds to a finite float (FLT_MAX at the most), one at or above it to an infinity. */
#define FLOAT_ROUNDING_LIMIT 3.4028235677973366e38


// Types.
/* The items of a value element's context menu. */
typedef enum ValueMenuItemEnum
{
    ValueMenuItem_Reset = 1,
    ValueMenuItem_Copy,
    ValueMenuItem_Paste
} ValueMenuItem;


// Static functions.
static bool IsFinite(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value);
}

static bool IsSpace(unsigned char byte)
{
    return (byte == (unsigned char)' ') || (byte == (unsigned char)'\t') || (byte == (unsigned char)'\n') || (byte == (unsigned char)'\r')
        || (byte == (unsigned char)'\v') || (byte == (unsigned char)'\f');
}

/* Trims spaces at both ends of [*start, *end). */
static void Trim(const unsigned char* text, size_t* start, size_t* end)
{
    while ((*start < *end) && IsSpace(text[*start]))
    {
        (*start)++;
    }
    while ((*end > *start) && IsSpace(text[*end - 1U]))
    {
        (*end)--;
    }
}

static uint64_t GetWidgetId(const UserData* userData)
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

static Error OnMenuItem(UIScreen* screen, const UIMenuActivation* activation, const UserData* userData)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, GetWidgetId(userData), &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result); // The element went away while its menu was open: nothing to do.
        return Error_CreateSuccess();
    }
    const UIValueSettings* Settings = UIValueCapability_GetSettings(Value);
    switch ((ValueMenuItem)activation->ItemId)
    {
        case ValueMenuItem_Reset:
            if (!Settings->HasDefault || !UIValueCapability_IsEnabled(Value))
            {
                return Error_CreateSuccess();
            }
            return UIValueCapability_ApplyWholeChange(Value, Target, UIValueSettings_Normalize(Settings, Settings->Default), NULL);
        case ValueMenuItem_Copy:
            return UIValueCapability_Copy(Value, Target);
        case ValueMenuItem_Paste:
        {
            const UIValueHooks* Hooks = Value->_hooks;
            const unsigned char* Unit = ((Hooks != NULL) && (Hooks->GetUnit != NULL)) ? Hooks->GetUnit(Target) : NULL;
            return UIValueCapability_IsEnabled(Value) ? UIValueCapability_Paste(Value, Target, Unit, NULL) : Error_CreateSuccess();
        }
    }
    return Error_CreateSuccess();
}

static UIMenuItem MakeItem(ValueMenuItem id, const char* label, bool isEnabled, const unsigned char* shortcut)
{
    UIMenuItem Item = UIMenuItem_Create((uint32_t)id, (const unsigned char*)label);
    Item.IsEnabled = isEnabled;
    Item.Shortcut = ((shortcut != NULL) && (shortcut[0] != 0U)) ? shortcut : NULL;
    return Item;
}

static Error ProvideMenu(UIScreen* screen, const UIContextMenuRequest* request, const UserData* userData)
{
    return UIValue_OpenContextMenu(screen, request, GetWidgetId(userData));
}


// Public functions: text.
Error UIValue_WriteShortestText(double value, bool isSinglePrecision, GenericBuffer* outText)
{
    if (isSinglePrecision && IsFinite(value) && (Math_AbsDouble(value) <= (double)FLT_MAX) && ((double)(float)value == value))
    {
        return Number_FloatToString((float)value, outText, DecimalFormatOptions_CreateShortest(DecimalSeparator_Period));
    }
    return Number_DoubleToString(value, outText, DecimalFormatOptions_CreateShortest(DecimalSeparator_Period));
}

Error UIValue_WriteFixedText(double value, int32_t precision, GenericBuffer* outText)
{
    int32_t Precision = (precision < 0) ? 0 : ((precision > UI_VALUE_MAX_PRECISION) ? UI_VALUE_MAX_PRECISION : precision);
    // A value that rounds to zero at this precision shows no sign: "-0.000" would read as a value of its own.
    double Unit = Math_PowDouble(10.0, (double)-Precision);
    double Value = (Math_AbsDouble(value) < (Unit * 0.5)) ? 0.0 : value;
    return Number_DoubleToString(Value, outText, DecimalFormatOptions_CreateFixed(DecimalSeparator_Period, Precision));
}

bool UIValue_ParseText(const unsigned char* text, size_t length, const unsigned char* unit, bool isSinglePrecision, double* outValue)
{
    if ((text == NULL) || (length == 0U))
    {
        return false;
    }
    size_t End = 0U;
    while ((End < length) && (text[End] != 0U))
    {
        End++;
    }
    if (End > UI_VALUE_MAX_TEXT_BYTES)
    {
        return false;
    }
    size_t Start = 0U;
    Trim(text, &Start, &End);
    size_t UnitLength = (unit != NULL) ? StringUTF8_GetByteLength(unit) : 0U;
    size_t UnitStart = 0U;
    size_t UnitEnd = UnitLength;
    if (UnitLength > 0U)
    {
        Trim(unit, &UnitStart, &UnitEnd);
    }
    size_t TrimmedUnitLength = UnitEnd - UnitStart;
    if ((TrimmedUnitLength > 0U) && ((End - Start) > TrimmedUnitLength)
        && Memory_IsEqual(text + (End - TrimmedUnitLength), unit + UnitStart, TrimmedUnitLength))
    {
        End -= TrimmedUnitLength;
        Trim(text, &Start, &End);
    }
    size_t Length = End - Start;
    if (Length == 0U)
    {
        return false;
    }
    unsigned char Buffer[PARSE_CAPACITY];
    Memory_Copy(text + Start, Buffer, Length);
    Buffer[Length] = 0U;
    double Value = 0.0;
    Error Result = Number_DoubleFromString(Buffer, &Value, DecimalSeparator_Period);
    bool IsNumber = (Result.Code == ErrorCode_Success) && IsFinite(Value);
    Error_Deconstruct(&Result);
    if (!IsNumber)
    {
        return false;
    }
    if (isSinglePrecision)
    {
        // A float's shortest text of FLT_MAX reads as a double a little above it, which still rounds to FLT_MAX as a float.
        double Size = Math_AbsDouble(Value);
        if (Size >= FLOAT_ROUNDING_LIMIT)
        {
            return false;
        }
        Value = (Size > (double)FLT_MAX) ? ((Value < 0.0) ? -(double)FLT_MAX : (double)FLT_MAX) : (double)(float)Value;
    }
    *outValue = Value;
    return true;
}


// Public functions: the clipboard and the menu.
Error UIValueCapability_Copy(const UIValueCapability* self, Widget* widget)
{
    const GameInput* Input = UIScreen_GetInput(Widget_GetScreen(widget));
    if (Input == NULL)
    {
        return Error_CreateSuccess();
    }
    unsigned char Bytes[64];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
    Error Result = UIValue_WriteShortestText(UIValueCapability_GetValue(self), UIValueCapability_GetSettings(self)->IsSinglePrecision, &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    return GameInput_SetClipboardText(Input, Bytes);
}

Error UIValueCapability_Paste(UIValueCapability* self, Widget* widget, const unsigned char* unit, bool* outIsPasted)
{
    if (outIsPasted != NULL)
    {
        *outIsPasted = false;
    }
    const GameInput* Input = UIScreen_GetInput(Widget_GetScreen(widget));
    if (Input == NULL)
    {
        return Error_CreateSuccess();
    }
    unsigned char Bytes[PARSE_CAPACITY];
    GenericBuffer Text;
    GenericBuffer_CreateConstant(&Text, Bytes, sizeof(Bytes), sizeof(unsigned char), 0U);
    // One byte past the longest number: a longer text is no number, and is told apart from one that fits.
    Error Result = GameInput_GetClipboardText(Input, UI_VALUE_MAX_TEXT_BYTES + 1U, &Text);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    const UIValueSettings* Settings = UIValueCapability_GetSettings(self);
    double Value = 0.0;
    if (!UIValue_ParseText(Bytes, Text._count, unit, Settings->IsSinglePrecision, &Value))
    {
        return UIElement_PostCue(widget, UIElementCue_Refused);
    }
    if (outIsPasted != NULL)
    {
        *outIsPasted = true;
    }
    return UIValueCapability_ApplyWholeChange(self, widget, UIValueSettings_Normalize(Settings, Value), NULL);
}

Error UIValue_OpenContextMenu(UIScreen* screen, const UIContextMenuRequest* request, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    if (UIValueCapability_IsChanging(Value))
    {
        return Error_CreateSuccess(); // A right click during a drag opens nothing: the menu would take the focus and end the drag.
    }
    bool IsEnabled = UIValueCapability_IsEnabled(Value);
    unsigned char Shortcuts[3][UI_MENU_SHORTCUT_CAPACITY];
    UIMenu_WriteShortcut(screen, GameAction_UIReset, Shortcuts[0], UI_MENU_SHORTCUT_CAPACITY);
    UIMenu_WriteShortcut(screen, GameAction_UICopy, Shortcuts[1], UI_MENU_SHORTCUT_CAPACITY);
    UIMenu_WriteShortcut(screen, GameAction_UIPaste, Shortcuts[2], UI_MENU_SHORTCUT_CAPACITY);
    UIMenuItem Items[] =
    {
        MakeItem(ValueMenuItem_Reset, "Reset", IsEnabled && UIValueCapability_GetSettings(Value)->HasDefault && !UIValueCapability_IsChanging(Value),
            Shortcuts[0]),
        UIMenuItem_Separator(),
        MakeItem(ValueMenuItem_Copy, "Copy", true, Shortcuts[1]),
        MakeItem(ValueMenuItem_Paste, "Paste", IsEnabled, Shortcuts[2]),
    };
    UserData Data = MakeData(widgetId);
    return UIMenu_OpenAt(screen, request, Items, sizeof(Items) / sizeof(Items[0]), OnMenuItem, &Data);
}

Error UIValue_AttachContextMenu(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIValueCapability* Value = NULL;
    Error Result = UIValue_Resolve(screen, widgetId, &Target, &Value);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UserData Data = MakeData(widgetId);
    return UIContextMenu_SetProvider(screen, widgetId, ProvideMenu, &Data);
}
