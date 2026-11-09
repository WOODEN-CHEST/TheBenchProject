#include "UIColorPicker.h"
#include "UIKit.h"
#include "UIKitInternal.h"
#include "UIPopup.h"
#include "UIScreen.h"
#include "UIWidgetFactory.h"
#include "wr/WRMath.h"
#include "wr/WRMemory.h"


// Macros.
/** The side of the square's marker and the thickness of a strip's, in UI pixels. */
#define MARKER_SIZE 8.0f
#define STRIP_MARKER_THICKNESS 4.0f
/** How far an arrow moves the square's or a strip's share, and with Shift. */
#define ARROW_STEP 0.01
#define ARROW_FINE_STEP 0.001
/** A channel field's places after the point: none for 0 to 255, two for a brightness or an opacity. */
#define FRACTION_PRECISION 2
/** The hue strip's six runs, each a straight blend in red, green and blue. */
#define HUE_RUNS ((size_t)6)


// Types.
/* What a field edits: a channel, or the hex field the whole colour. */
typedef enum ChannelEnum
{
    Channel_Red,
    Channel_Green,
    Channel_Blue,
    Channel_Alpha,
    Channel_Brightness,
    Channel_Opacity,
    Channel_Hex
} Channel;

/* Which strip a strip is. */
typedef enum StripAxisEnum
{
    StripAxis_Hue,
    StripAxis_Alpha
} StripAxis;

/* What the popup's parts are made with: the kit, the picker, the variant of its look. */
typedef struct PopupArgsStruct
{
    UIKit* Kit;
    uint64_t PickerId;
    const unsigned char* Variant;
} PopupArgs;

typedef struct PartArgsStruct
{
    uint64_t PickerId;
    StripAxis Axis;
    const unsigned char* Variant;
} PartArgs;

/* The popup: a popup's root holding the square, the strips, a row of labelled fields and the hex field. */
typedef struct ColorPopupStruct
{
    Widget Base;
    UIElementLook Look;
    uint64_t PickerId;
    uint64_t SquareId;
    uint64_t HueId;
    uint64_t AlphaId;
    uint64_t FieldRowId;
    uint64_t FieldIds[UI_COLOR_PICKER_MAX_FIELDS];
    Channel FieldChannels[UI_COLOR_PICKER_MAX_FIELDS];
    size_t FieldCount;
    uint64_t HexRowId;
    uint64_t HexId;
    uint64_t ShownRevision;
    bool HasShown;
} ColorPopup;

/* The square of saturation (across) and value (up), or a strip of hue or alpha: dragged by the left button. */
typedef struct ColorAreaStruct
{
    Widget Base;
    UIElementLook Look;
    uint64_t PickerId;
    StripAxis Axis;
    bool IsSquare;
    bool IsDragging;
} ColorArea;


// Static functions: the conversions.
static double Clamp01(double value)
{
    return Math_IsNaNDouble(value) ? 0.0 : Math_ClampDouble(value, 0.0, 1.0);
}

static unsigned char ToByte(double share)
{
    return (unsigned char)Math_FloorDouble((Clamp01(share) * 255.0) + 0.5);
}

static float ClampFloat01(float value)
{
    return Math_IsNaNFloat(value) ? 0.0f : Math_ClampFloat(value, 0.0f, 1.0f);
}

static int32_t HexDigit(unsigned char character)
{
    if ((character >= (unsigned char)'0') && (character <= (unsigned char)'9'))
    {
        return (int32_t)(character - (unsigned char)'0');
    }
    if ((character >= (unsigned char)'a') && (character <= (unsigned char)'f'))
    {
        return 10 + (int32_t)(character - (unsigned char)'a');
    }
    if ((character >= (unsigned char)'A') && (character <= (unsigned char)'F'))
    {
        return 10 + (int32_t)(character - (unsigned char)'A');
    }
    return -1;
}


// Static functions: helpers.
static UIColorPickerCapability* GetCapability(void* widget)
{
    return &((UIColorPickerLookBase*)widget)->Picker;
}

static Error ResolvePicker(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIColorPickerCapability** outPicker)
{
    UIKit* Kit = (screen != NULL) ? UIKit_FromScreen(screen) : NULL;
    Widget* Target = NULL;
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(screen, widgetId, &Target))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIColorPicker: the widget is gone, or its screen has no kit.");
    }
    UIColorPickerCapability* Picker = Widget_GetCapability(Target, UIKit_GetCapabilityId(Kit, UIElementCapability_ColorPicker));
    if (Picker == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIColorPicker: the widget is not a colour picker.");
    }
    *outWidget = Target;
    *outPicker = Picker;
    return Error_CreateSuccess();
}

/* The picker a part belongs to, and its capability; false when it is gone. */
static bool TryGetPicker(const Widget* part, uint64_t pickerId, Widget** outPicker, UIColorPickerCapability** outCapability)
{
    Widget* Picker = NULL;
    UIScreen* Screen = Widget_GetScreen(part);
    UIKit* Kit = UIKit_FromScreen(Screen);
    if ((Kit == NULL) || !UIScreen_TryGetWidgetById(Screen, pickerId, &Picker))
    {
        return false;
    }
    UIColorPickerCapability* Capability = Widget_GetCapability(Picker, UIKit_GetCapabilityId(Kit, UIElementCapability_ColorPicker));
    if (Capability == NULL)
    {
        return false;
    }
    *outPicker = Picker;
    *outCapability = Capability;
    return true;
}

/* The value as an event carries it: a Color, or a RenderColor. */
static UIPropertyValue GetEventValue(const UIColorPickerCapability* self)
{
    if (self->_kind == UIColorKind_RenderColor)
    {
        RenderColor Value = { .Tint = self->_color, .Brightness = self->_brightness, .Opacity = self->_opacity };
        return UIPropertyValue_FromRenderColor(Value);
    }
    return UIPropertyValue_FromColor(self->_color);
}

static Error PostValueEvent(Widget* widget, const UIColorPickerCapability* self, UIElementEvent event)
{
    UIPropertyValue Value = GetEventValue(self);
    return UIScreen_PostWidgetEvent(Widget_GetScreen(widget), Widget_GetId(widget), (int32_t)event, &Value);
}

/* The HSV of a colour set from its channels, keeping the hue through grey and the saturation through black. */
static void SyncHsv(UIColorPickerCapability* self)
{
    UIColorHsv Hsv = UIColor_ToHsv(self->_color);
    unsigned char Highest = (unsigned char)Math_MaxInt32(self->_color.r, Math_MaxInt32(self->_color.g, self->_color.b));
    unsigned char Lowest = (unsigned char)Math_MinInt32(self->_color.r, Math_MinInt32(self->_color.g, self->_color.b));
    if (Highest == Lowest)
    {
        Hsv.Hue = self->_hsv.Hue;
        Hsv.Saturation = (Highest == 0U) ? self->_hsv.Saturation : 0.0;
    }
    self->_hsv = Hsv;
}

/* The user moved the colour (the popup's change is in progress): the mixed state ends, and Changed. Nothing when it came out the same. */
static Error MoveTo(Widget* widget, UIColorPickerCapability* self, Color color, float brightness, float opacity)
{
    bool IsSame = !self->_isMixed && (color.r == self->_color.r) && (color.g == self->_color.g) && (color.b == self->_color.b)
        && (color.a == self->_color.a) && (brightness == self->_brightness) && (opacity == self->_opacity);
    if (IsSame)
    {
        return Error_CreateSuccess();
    }
    self->_color = color;
    self->_brightness = brightness;
    self->_opacity = opacity;
    self->_isMixed = false;
    self->_revision++;
    return PostValueEvent(widget, self, UIElementEvent_Changed);
}

/* The popup opens: the change starts with the value it opened with. */
static void BeginChange(UIColorPickerCapability* self)
{
    self->_isChanging = true;
    self->_startColor = self->_color;
    self->_startBrightness = self->_brightness;
    self->_startOpacity = self->_opacity;
    self->_startIsMixed = self->_isMixed;
}

/* The popup closed keeping the colour: Committed when it is not what it opened with. */
static Error EndChange(Widget* widget, UIColorPickerCapability* self)
{
    if (!self->_isChanging)
    {
        return Error_CreateSuccess();
    }
    self->_isChanging = false;
    bool IsSame = (self->_isMixed == self->_startIsMixed) && (self->_color.r == self->_startColor.r) && (self->_color.g == self->_startColor.g)
        && (self->_color.b == self->_startColor.b) && (self->_color.a == self->_startColor.a) && (self->_brightness == self->_startBrightness)
        && (self->_opacity == self->_startOpacity);
    return IsSame ? Error_CreateSuccess() : PostValueEvent(widget, self, UIElementEvent_Committed);
}

/* Escape: the value it opened with comes back, with Cancelled. */
static Error CancelChange(Widget* widget, UIColorPickerCapability* self)
{
    if (!self->_isChanging)
    {
        return Error_CreateSuccess();
    }
    self->_isChanging = false;
    self->_color = self->_startColor;
    self->_brightness = self->_startBrightness;
    self->_opacity = self->_startOpacity;
    self->_isMixed = self->_startIsMixed;
    SyncHsv(self);
    self->_revision++;
    return PostValueEvent(widget, self, UIElementEvent_Cancelled);
}

static float GetAlphaShare(const UIColorPickerCapability* self)
{
    return (self->_kind == UIColorKind_RenderColor) ? self->_opacity : ((float)self->_color.a / 255.0f);
}

static UserData MakeFieldData(uint64_t pickerId, uint32_t field)
{
    UserData Data = UserData_CreateEmpty();
    Memory_Copy(&pickerId, Data.Bytes, sizeof(pickerId));
    Memory_Copy(&field, Data.Bytes + sizeof(pickerId), sizeof(field));
    return Data;
}

static void ReadFieldData(const UserData* data, uint64_t* outPickerId, uint32_t* outField)
{
    Memory_Copy(data->Bytes, outPickerId, sizeof(*outPickerId));
    Memory_Copy(data->Bytes + sizeof(*outPickerId), outField, sizeof(*outField));
}

static const UIThemeStyle* GetStyle(const UIElementLook* look, const Widget* widget)
{
    return UIElementLook_GetStyle(look, Widget_GetScreen(widget));
}

static float RoundToPixel(float value)
{
    return Math_FloorFloat(value + 0.5f);
}


// Static functions: the popup's fields.
static ColorPopup* GetPopup(UIScreen* screen, const UIColorPickerCapability* picker)
{
    Widget* Found = NULL;
    return UIScreen_TryGetWidgetById(screen, picker->_popupId, &Found) ? (ColorPopup*)(void*)Found : NULL;
}

/* A field's value, as the picker holds it. */
static double GetChannelValue(const UIColorPickerCapability* self, Channel channel)
{
    switch (channel)
    {
        case Channel_Red:
            return (double)self->_color.r;
        case Channel_Green:
            return (double)self->_color.g;
        case Channel_Blue:
            return (double)self->_color.b;
        case Channel_Alpha:
            return (double)self->_color.a;
        case Channel_Brightness:
            return (double)self->_brightness;
        case Channel_Opacity:
            return (double)self->_opacity;
        case Channel_Hex:
            return 0.0;
    }
    return 0.0;
}

/* A field changed: its channel takes its value. */
static Error ApplyChannel(Widget* widget, UIColorPickerCapability* self, Channel channel, double value)
{
    Color Moved = self->_color;
    float Brightness = self->_brightness;
    float Opacity = self->_opacity;
    unsigned char Byte = (unsigned char)Math_ClampDouble(Math_FloorDouble(value + 0.5), 0.0, 255.0);
    switch (channel)
    {
        case Channel_Red:
            Moved.r = Byte;
            break;
        case Channel_Green:
            Moved.g = Byte;
            break;
        case Channel_Blue:
            Moved.b = Byte;
            break;
        case Channel_Alpha:
            Moved.a = Byte;
            break;
        case Channel_Brightness:
            Brightness = ClampFloat01((float)value);
            break;
        case Channel_Opacity:
            Opacity = ClampFloat01((float)value);
            break;
        case Channel_Hex:
            break;
    }
    Error Result = MoveTo(widget, self, Moved, Brightness, Opacity);
    SyncHsv(self);
    return Result;
}

/* The hex field's text: a colour moves the picker at once (as it is typed); text that reads as none shows the invalid state, and at the
 * end of the visit is put back (the refused cue). */
static Error ApplyHex(UIScreen* screen, Widget* picker, UIColorPickerCapability* self, uint64_t hexId, bool isVisitEnd)
{
    const unsigned char* Text = NULL;
    size_t Length = 0U;
    Error Result = UITextBox_GetText(screen, hexId, &Text, &Length);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess(); // Gone with its popup: what it read as was applied while it was typed.
    }
    Color Read = { .r = 0U, .g = 0U, .b = 0U, .a = 255U };
    bool HasAlpha = false;
    bool IsColor = UIColor_TryParseHex(Text, Length, &Read, &HasAlpha);
    Result = UITextBox_SetIsInvalid(screen, hexId, !IsColor && !isVisitEnd && (Length > 0U));
    if ((Result.Code != ErrorCode_Success) || !IsColor)
    {
        if ((Result.Code == ErrorCode_Success) && isVisitEnd)
        {
            self->_revision++; // Put back: the popup writes the colour's hex again.
            Result = UIElement_PostCue(picker, UIElementCue_Refused);
        }
        return Result;
    }
    bool TakesAlpha = HasAlpha && self->_hasAlpha;
    float Opacity = (TakesAlpha && (self->_kind == UIColorKind_RenderColor)) ? ((float)Read.a / 255.0f) : self->_opacity;
    Read.a = (TakesAlpha && (self->_kind == UIColorKind_Color)) ? Read.a : self->_color.a;
    Result = MoveTo(picker, self, Read, self->_brightness, Opacity);
    SyncHsv(self);
    return Result;
}

/* A field's own events: its value becomes the picker's channel (read from the event, so a field gone with its popup still counts), the
 * hex field's text the whole colour. */
static Error OnFieldEvent(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    bool IsChange = (event->WidgetEventId == (int32_t)UIElementEvent_Changed) || (event->WidgetEventId == (int32_t)UIElementEvent_Committed);
    if (!IsChange)
    {
        return Error_CreateSuccess();
    }
    uint64_t PickerId = WIDGET_ID_INVALID;
    uint32_t Field = 0U;
    ReadFieldData(userData, &PickerId, &Field);
    Widget* Picker = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, PickerId, &Picker, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    if (!Self->_isChanging)
    {
        return Error_CreateSuccess();
    }
    if ((Channel)Field == Channel_Hex)
    {
        return ApplyHex(screen, Picker, Self, event->WidgetId, event->WidgetEventId == (int32_t)UIElementEvent_Committed);
    }
    if (!event->HasValue || (event->Value.Type != UIPropertyType_Double))
    {
        return Error_CreateSuccess();
    }
    return ApplyChannel(Picker, Self, (Channel)Field, event->Value.Data.DoubleValue);
}

/* The fields follow the picker's value: those not being edited are set by code (no events). */
static Error SyncFields(ColorPopup* popup, const UIColorPickerCapability* picker)
{
    if (popup->HasShown && (popup->ShownRevision == picker->_revision))
    {
        return Error_CreateSuccess();
    }
    UIScreen* Screen = Widget_GetScreen(&popup->Base);
    uint64_t Entered = UIScreen_GetEnteredWidgetId(Screen);
    Error Result = Error_CreateSuccess();
    for (size_t Index = 0U; (Index < popup->FieldCount) && (Result.Code == ErrorCode_Success); Index++)
    {
        if (popup->FieldIds[Index] != Entered)
        {
            Result = UIValue_Set(Screen, popup->FieldIds[Index], GetChannelValue(picker, popup->FieldChannels[Index]));
        }
    }
    if ((Result.Code == ErrorCode_Success) && (popup->HexId != Entered))
    {
        unsigned char Hex[UI_COLOR_HEX_BYTES];
        Color Shown = picker->_color;
        if (picker->_kind == UIColorKind_RenderColor)
        {
            Shown.a = ToByte((double)picker->_opacity);
        }
        UIColor_WriteHex(Shown, picker->_hasAlpha, Hex);
        Result = UITextBox_SetText(Screen, popup->HexId, Hex);
    }
    popup->HasShown = true;
    popup->ShownRevision = picker->_revision;
    return Result;
}


// Static functions: the swatch (the kind's behaviour).
static void* ResolvePickerCapability(void* widget)
{
    return GetCapability(widget);
}

static Error OpenPopup(UIScreen* screen, uint64_t pickerId);

static Error ClosePopup(UIScreen* screen, UIColorPickerCapability* self)
{
    uint64_t PopupId = self->_popupId;
    return UIScreen_IsPopupOpen(screen, PopupId) ? UIScreen_ClosePopup(screen, PopupId) : Error_CreateSuccess();
}

static Error Behaviour_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIColorPicker: the arguments are required.");
    }
    const UIColorPickerOptions* Options = ((const UIColorPickerArgs*)args)->Options;
    bool IsKnownKind = (Options->Kind == UIColorKind_Color) || (Options->Kind == UIColorKind_RenderColor);
    if (!IsKnownKind)
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "UIColorPicker: an unknown kind of value.");
    }
    Error Result = UIElement_ApplyOptions(widget, &Options->Element);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    UIColorPickerCapability* Self = GetCapability(widget);
    Self->_press = UIPressable_Create();
    Self->_kind = Options->Kind;
    Self->_hasAlpha = Options->HasAlpha;
    Self->_color = (Options->Kind == UIColorKind_RenderColor) ? Options->RenderColor.Tint : Options->Color;
    Self->_brightness = (Options->Kind == UIColorKind_RenderColor) ? ClampFloat01(Options->RenderColor.Brightness) : 1.0f;
    Self->_opacity = (Options->Kind == UIColorKind_RenderColor) ? ClampFloat01(Options->RenderColor.Opacity) : 1.0f;
    Self->_hsv = (UIColorHsv){ .Hue = 0.0, .Saturation = 0.0, .Value = 0.0 };
    SyncHsv(Self);
    Self->_isMixed = Options->IsMixed;
    Self->_isEnabled = Options->IsEnabled;
    Self->_popupId = WIDGET_ID_INVALID;
    Self->_isChanging = false;
    Self->_startColor = Self->_color;
    Self->_startBrightness = Self->_brightness;
    Self->_startOpacity = Self->_opacity;
    Self->_startIsMixed = Self->_isMixed;
    Self->_revision = 1U;
    if (!Options->IsEnabled)
    {
        Error Focus = Widget_SetIsFocusable(widget, false); // The widget is not alive yet: nothing can fail or run.
        Error_Deconstruct(&Focus);
    }
    return Error_CreateSuccess();
}

static Error Behaviour_Update(void* widget, ProgramTime time)
{
    (void)time;
    UIColorPickerCapability* Self = GetCapability(widget);
    UIScreen* Screen = Widget_GetScreen(widget);
    UIPressable_Update(&Self->_press, Screen);
    if ((Self->_popupId != WIDGET_ID_INVALID) && !UIScreen_IsPopupOpen(Screen, Self->_popupId))
    {
        Self->_popupId = WIDGET_ID_INVALID; // Closed; what it kept or put back is the closing's (OnPopupClosed).
    }
    return Error_CreateSuccess();
}

static Error Behaviour_OnHoverStart(void* widget, const WidgetHoverArgs* args)
{
    (void)args;
    return UIElement_PostCue(widget, UIElementCue_Hover);
}

static Error Behaviour_OnMouseInput(void* widget, const WidgetMouseInputArgs* args)
{
    UIColorPickerCapability* Self = GetCapability(widget);
    Error Result = Self->_isEnabled ? UIElement_PostMouseEvents(widget, args) : Error_CreateSuccess();
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIElement_PostMouseCues(widget, args, Self->_isEnabled);
    }
    bool IsActivated = false;
    UIPressable_OnMouseInput(&Self->_press, widget, args, Self->_isEnabled, &IsActivated);
    if ((Result.Code != ErrorCode_Success) || !IsActivated)
    {
        return Result;
    }
    UIScreen* Screen = Widget_GetScreen(widget);
    return UIScreen_IsPopupOpen(Screen, Self->_popupId) ? ClosePopup(Screen, Self) : OpenPopup(Screen, Widget_GetId(widget));
}

static Error Behaviour_OnPressLost(void* widget, GameKey button)
{
    UIPressable_OnPressLost(&GetCapability(widget)->_press, button);
    return Error_CreateSuccess();
}

static Error Behaviour_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    UIColorPickerCapability* Self = GetCapability(widget);
    bool IsActivated = false;
    UIPressable_OnKeyboardInput(&Self->_press, widget, args, Self->_isEnabled, outIsHandled, &IsActivated);
    if (IsActivated)
    {
        return OpenPopup(Widget_GetScreen(widget), Widget_GetId(widget));
    }
    return (Self->_isEnabled && !*outIsHandled) ? UIElement_OnContextKey(widget, args, outIsHandled) : Error_CreateSuccess();
}

/* The popup closed: by Escape the colour it opened with comes back; any other way it is kept. */
static Error OnPopupClosed(UIScreen* screen, const UIEvent* event, const UserData* userData)
{
    (void)userData;
    Widget* Picker = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, event->WidgetId, &Picker, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&Result);
        return Error_CreateSuccess();
    }
    return (event->WidgetEventId == (int32_t)UIPopupCloseReason_Escape) ? CancelChange(Picker, Self) : EndChange(Picker, Self);
}


// Static functions: the popup.
static Error Popup_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIColorPicker: the popup's arguments are required.");
    }
    const PopupArgs* Arguments = args;
    ColorPopup* Popup = widget;
    Error Result = UIElementLook_Construct(&Popup->Look, UIElementKind_ColorPicker, Arguments->Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Popup->PickerId = Arguments->PickerId;
    Popup->SquareId = WIDGET_ID_INVALID;
    Popup->HueId = WIDGET_ID_INVALID;
    Popup->AlphaId = WIDGET_ID_INVALID;
    Popup->FieldRowId = WIDGET_ID_INVALID;
    Popup->FieldCount = 0U;
    Popup->HexRowId = WIDGET_ID_INVALID;
    Popup->HexId = WIDGET_ID_INVALID;
    Popup->ShownRevision = 0U;
    Popup->HasShown = false;
    return Error_CreateSuccess();
}

static Error Popup_Update(void* self, ProgramTime time)
{
    ColorPopup* Popup = self;
    UIElementLook_Update(&Popup->Look, Widget_GetScreen(&Popup->Base), UIElementState_Normal, time.PassedTime);
    Widget* Picker = NULL;
    UIColorPickerCapability* Capability = NULL;
    return TryGetPicker(&Popup->Base, Popup->PickerId, &Picker, &Capability) ? SyncFields(Popup, Capability) : Error_CreateSuccess();
}

/* The sizes its parts take from the theme, in its UI pixels. */
typedef struct PopupMetricsStruct
{
    Vector2 Padding;
    float Spacing;
    float Square;
    float Strip;
} PopupMetrics;

static PopupMetrics GetPopupMetrics(const ColorPopup* popup)
{
    const UIThemeStyle* Style = GetStyle(&popup->Look, &popup->Base);
    return (PopupMetrics){ .Padding = UIThemeStyle_GetVector(Style, UIElementKind_ColorPicker, UIColorPickerPart_Padding),
        .Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_ColorPicker, UIColorPickerPart_Spacing),
        .Square = UIThemeStyle_GetNumber(Style, UIElementKind_ColorPicker, UIColorPickerPart_SquareSize),
        .Strip = UIThemeStyle_GetNumber(Style, UIElementKind_ColorPicker, UIColorPickerPart_StripWidth) };
}

static float GetSquareRowWidth(const ColorPopup* popup, const PopupMetrics* metrics)
{
    float Strips = (popup->AlphaId != WIDGET_ID_INVALID) ? 2.0f : 1.0f;
    return metrics->Square + (Strips * (metrics->Spacing + metrics->Strip));
}

static Error MeasureChild(UIScreen* screen, uint64_t id, const UIMeasureContext* context, float width, Vector2* outSize)
{
    *outSize = (Vector2){ .x = 0.0f, .y = 0.0f };
    Widget* Child = NULL;
    return UIScreen_TryGetWidgetById(screen, id, &Child) ? Widget_Measure(Child, context, width, outSize) : Error_CreateSuccess();
}

static Error Popup_Measure(void* self, const UIMeasureContext* context, float availableWidth, Vector2* outSize)
{
    (void)availableWidth;
    ColorPopup* Popup = self;
    UIScreen* Screen = Widget_GetScreen(&Popup->Base);
    PopupMetrics Metrics = GetPopupMetrics(Popup);
    Vector2 Fields = { .x = 0.0f, .y = 0.0f };
    Vector2 Hex = { .x = 0.0f, .y = 0.0f };
    Error Result = MeasureChild(Screen, Popup->FieldRowId, context, UI_MEASURE_UNBOUNDED_WIDTH, &Fields);
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasureChild(Screen, Popup->HexRowId, context, UI_MEASURE_UNBOUNDED_WIDTH, &Hex);
    }
    float Inner = Math_MaxFloat(GetSquareRowWidth(Popup, &Metrics), Math_MaxFloat(Fields.x, Hex.x));
    outSize->x = Inner + (2.0f * Metrics.Padding.x);
    outSize->y = (2.0f * Metrics.Padding.y) + Metrics.Square + (2.0f * Metrics.Spacing) + Fields.y + Hex.y;
    return Result;
}

static Error PlaceChild(UIScreen* screen, uint64_t id, float x, float y, float width, float height)
{
    Widget* Child = NULL;
    if (!UIScreen_TryGetWidgetById(screen, id, &Child))
    {
        return Error_CreateSuccess();
    }
    UIPlacement Placement = UIPlacement_FromTopLeft(x, y, Math_MaxFloat(0.0f, width), Math_MaxFloat(0.0f, height));
    return Widget_SetPlacement(Child, &Placement);
}

/* The square at the top left, the strips beside it; the fields' row under them; the hex row under that. */
static Error Popup_Layout(void* self, const UILayoutContext* context)
{
    ColorPopup* Popup = self;
    UIScreen* Screen = Widget_GetScreen(&Popup->Base);
    PopupMetrics Metrics = GetPopupMetrics(Popup);
    Rectangle Pixels = UIScreen_GetWidgetPixelBox(Screen, &Popup->Base);
    float Inner = Math_MaxFloat(0.0f, (Pixels.width / Widget_GetEffectiveScale(&Popup->Base)) - (2.0f * Metrics.Padding.x));
    float X = Metrics.Padding.x;
    float Y = Metrics.Padding.y;
    Error Result = PlaceChild(Screen, Popup->SquareId, X, Y, Metrics.Square, Metrics.Square);
    X += Metrics.Square + Metrics.Spacing;
    if (Result.Code == ErrorCode_Success)
    {
        Result = PlaceChild(Screen, Popup->HueId, X, Y, Metrics.Strip, Metrics.Square);
    }
    X += Metrics.Strip + Metrics.Spacing;
    if (Result.Code == ErrorCode_Success)
    {
        Result = PlaceChild(Screen, Popup->AlphaId, X, Y, Metrics.Strip, Metrics.Square);
    }
    Y += Metrics.Square + Metrics.Spacing;
    Vector2 Fields = { .x = 0.0f, .y = 0.0f };
    Vector2 Hex = { .x = 0.0f, .y = 0.0f };
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasureChild(Screen, Popup->FieldRowId, context->Sizing, Inner, &Fields);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = PlaceChild(Screen, Popup->FieldRowId, Metrics.Padding.x, Y, Inner, Fields.y);
    }
    Y += Fields.y + Metrics.Spacing;
    if (Result.Code == ErrorCode_Success)
    {
        Result = MeasureChild(Screen, Popup->HexRowId, context->Sizing, Inner, &Hex);
    }
    return (Result.Code == ErrorCode_Success) ? PlaceChild(Screen, Popup->HexRowId, Metrics.Padding.x, Y, Inner, Hex.y) : Result;
}

static Error Popup_Render(void* self, UIRenderContext* context)
{
    ColorPopup* Popup = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    if (UIElement_IsBoxEmpty(Pixels))
    {
        return Error_CreateSuccess();
    }
    return UIElementLook_DrawBox(&Popup->Look, GetStyle(&Popup->Look, &Popup->Base), UIColorPickerPart_Popup, context, Pixels,
        Widget_GetEffectiveScale(&Popup->Base));
}

/* Enter keeps and closes, Escape puts the colour back and closes, in one press from inside an entered field: Escape leaves it with a
 * cancel first; closing leaves it keeping (UIPopup.h), so its value arrives before the closing's own. */
static Error Popup_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    ColorPopup* Popup = self;
    UIScreen* Screen = Widget_GetScreen(&Popup->Base);
    if ((args->Type != UIKeyboardInputType_KeyPress) || args->IsHoverDelivery)
    {
        return Error_CreateSuccess();
    }
    bool IsAccept = UIElement_IsActionKey(Screen, GameAction_UIAccept, args->Key);
    bool IsBack = UIElement_IsActionKey(Screen, GameAction_UIBack, args->Key);
    if (!IsAccept && !IsBack)
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    uint64_t PopupId = Widget_GetId(&Popup->Base);
    uint64_t PickerId = Popup->PickerId;
    uint64_t Entered = UIScreen_GetEnteredWidgetId(Screen);
    bool IsEnteredInside = (Entered != WIDGET_ID_INVALID) && (UIScreen_FindPopupOf(Screen, Entered) == PopupId);
    Error Result = Error_CreateSuccess();
    if (IsBack)
    {
        Result = IsEnteredInside ? UIScreen_LeaveWidget(Screen, UILeaveReason_Cancel) : Error_CreateSuccess();
        Widget* Picker = NULL;
        UIColorPickerCapability* Capability = NULL;
        if ((Result.Code == ErrorCode_Success) && (ResolvePicker(Screen, PickerId, &Picker, &Capability).Code == ErrorCode_Success))
        {
            Result = CancelChange(Picker, Capability);
        }
    }
    if ((Result.Code == ErrorCode_Success) && UIScreen_IsPopupOpen(Screen, PopupId))
    {
        Result = UIScreen_ClosePopup(Screen, PopupId);
    }
    return Result;
}


// Static functions: the square and the strips.
static Error Area_Construct(void* widget, void* args)
{
    if (args == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIColorPicker: a part's arguments are required.");
    }
    const PartArgs* Arguments = args;
    ColorArea* Area = widget;
    Error Result = UIElementLook_Construct(&Area->Look, UIElementKind_ColorPicker, Arguments->Variant);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Area->PickerId = Arguments->PickerId;
    Area->Axis = Arguments->Axis;
    Area->IsSquare = false;
    Area->IsDragging = false;
    return Error_CreateSuccess();
}

static Error Square_Construct(void* widget, void* args)
{
    Error Result = Area_Construct(widget, args);
    if (Result.Code == ErrorCode_Success)
    {
        ((ColorArea*)widget)->IsSquare = true;
    }
    return Result;
}

static Error Area_Update(void* self, ProgramTime time)
{
    ColorArea* Area = self;
    UIElementState State = UIElementState_Choose(true, Area->IsDragging, false, Widget_IsEntered(&Area->Base), false, Widget_IsHovered(&Area->Base),
        Widget_IsFocused(&Area->Base));
    UIElementLook_Update(&Area->Look, Widget_GetScreen(&Area->Base), State, time.PassedTime);
    return Error_CreateSuccess();
}

/* The pointer moves the colour: saturation and value on the square, hue or alpha on a strip (top highest). */
static Error Area_MoveTo(ColorArea* area, Vector2 widgetPosition)
{
    Widget* Picker = NULL;
    UIColorPickerCapability* Self = NULL;
    if (!TryGetPicker(&area->Base, area->PickerId, &Picker, &Self) || !Self->_isChanging)
    {
        return Error_CreateSuccess();
    }
    double Across = Clamp01((double)widgetPosition.x);
    double Down = Clamp01((double)widgetPosition.y);
    UIColorHsv Hsv = Self->_hsv;
    float Opacity = Self->_opacity;
    unsigned char Alpha = Self->_color.a;
    if (area->IsSquare)
    {
        Hsv.Saturation = Across;
        Hsv.Value = 1.0 - Down;
    }
    else if (area->Axis == StripAxis_Hue)
    {
        Hsv.Hue = (Down >= 1.0) ? 0.0 : (Down * 360.0);
    }
    else if (Self->_kind == UIColorKind_RenderColor)
    {
        Opacity = (float)(1.0 - Down);
    }
    else
    {
        Alpha = ToByte(1.0 - Down);
    }
    Color Moved = UIColor_FromHsv(Hsv, Alpha);
    Error Result = MoveTo(Picker, Self, Moved, Self->_brightness, Opacity);
    Self->_hsv = Hsv; // The hue and the saturation as dragged, also where the colour is grey or black.
    return Result;
}

static Error Area_OnMouseInput(void* self, const WidgetMouseInputArgs* args)
{
    ColorArea* Area = self;
    if (args->Button != GameKey_MouseLeft)
    {
        return Error_CreateSuccess();
    }
    switch (args->Type)
    {
        case UIMouseInputType_ButtonPress:
            Area->IsDragging = true;
            return Area_MoveTo(Area, args->WidgetPosition);
        case UIMouseInputType_Move:
            return Area->IsDragging ? Area_MoveTo(Area, args->WidgetPosition) : Error_CreateSuccess();
        case UIMouseInputType_ButtonRelease:
            Area->IsDragging = false;
            return Error_CreateSuccess();
        case UIMouseInputType_ReleaseOver:
            return Error_CreateSuccess();
    }
    return Error_CreateSuccess();
}

/* Entered, the arrows move the colour by a step (Shift a fine one): the square's saturation across and value up, a strip's share down. */
static Error Area_OnKeyboardInput(void* self, const WidgetKeyboardInputArgs* args, bool* outIsHandled)
{
    ColorArea* Area = self;
    UIScreen* Screen = Widget_GetScreen(&Area->Base);
    bool IsDown = (args->Type == UIKeyboardInputType_KeyPress) || (args->Type == UIKeyboardInputType_KeyRepeat);
    Widget* Picker = NULL;
    UIColorPickerCapability* Self = NULL;
    if (!IsDown || !Widget_IsEntered(&Area->Base) || !TryGetPicker(&Area->Base, Area->PickerId, &Picker, &Self))
    {
        return Error_CreateSuccess();
    }
    double Across = 0.0;
    double Down = 0.0;
    if (UIElement_IsActionKey(Screen, GameAction_UILeft, args->Key))
    {
        Across = -1.0;
    }
    else if (UIElement_IsActionKey(Screen, GameAction_UIRight, args->Key))
    {
        Across = 1.0;
    }
    else if (UIElement_IsActionKey(Screen, GameAction_UIUp, args->Key))
    {
        Down = -1.0;
    }
    else if (UIElement_IsActionKey(Screen, GameAction_UIDown, args->Key))
    {
        Down = 1.0;
    }
    if ((Across == 0.0) && (Down == 0.0))
    {
        return Error_CreateSuccess();
    }
    *outIsHandled = true;
    double Step = ((args->Modifiers & GAME_KEY_MODIFIER_SHIFT) != 0U) ? ARROW_FINE_STEP : ARROW_STEP;
    // Where the colour stands on the part now, as a share of its box, moved by the step.
    Vector2 Place = { .x = (float)Self->_hsv.Saturation, .y = (float)(1.0 - Self->_hsv.Value) };
    if (!Area->IsSquare)
    {
        double Share = (Area->Axis == StripAxis_Hue) ? (Self->_hsv.Hue / 360.0) : (1.0 - (double)GetAlphaShare(Self));
        Place = (Vector2){ .x = 0.5f, .y = (float)Share };
        Down += Across; // A strip runs down: Left and Right move it as Up and Down do.
        Across = 0.0;
    }
    Place.x = (float)Clamp01((double)Place.x + (Across * Step));
    Place.y = (float)Clamp01((double)Place.y + (Down * Step));
    if (!Area->IsSquare && (Area->Axis == StripAxis_Hue) && (Place.y >= 1.0f))
    {
        Place.y = 1.0f - (float)(ARROW_FINE_STEP * 0.5); // The bottom is red again: the hue stops short of it, so Down never wraps.
    }
    Error Result = Area_MoveTo(Area, Place);
    return (Result.Code == ErrorCode_Success) ? UIElement_PostCue(&Area->Base, UIElementCue_ValueStep) : Result;
}

static Error Area_OnPressLost(void* self, GameKey button)
{
    if (button == GameKey_MouseLeft)
    {
        ((ColorArea*)self)->IsDragging = false;
    }
    return Error_CreateSuccess();
}

/* Two colours of squares behind what is not opaque, inside a box. */
static void DrawChecker(UIRenderContext* context, const UIThemeStyle* style, Rectangle box, float scale)
{
    float Side = Math_MaxFloat(1.0f, RoundToPixel(UIThemeStyle_GetNumber(style, UIElementKind_ColorPicker, UIColorPickerPart_CheckerSize) * scale));
    const UIStateColors* Light = UIThemeStyle_GetColors(style, UIColorPickerPart_CheckerLight);
    const UIStateColors* Dark = UIThemeStyle_GetColors(style, UIColorPickerPart_CheckerDark);
    Color LightColor = (Light != NULL) ? UIStateColors_Resolve(Light, UIElementState_Normal, WHITE) : WHITE;
    Color DarkColor = (Dark != NULL) ? UIStateColors_Resolve(Dark, UIElementState_Normal, GRAY) : GRAY;
    UIRenderContext_RenderPixelRectangle(context, box, LightColor);
    int32_t Columns = (int32_t)Math_CeilFloat(box.width / Side);
    int32_t Rows = (int32_t)Math_CeilFloat(box.height / Side);
    for (int32_t Row = 0; Row < Rows; Row++)
    {
        for (int32_t Column = (Row % 2); Column < Columns; Column += 2)
        {
            float X = box.x + ((float)Column * Side);
            float Y = box.y + ((float)Row * Side);
            Rectangle Cell = { .x = X, .y = Y, .width = Math_MinFloat(Side, (box.x + box.width) - X), .height = Math_MinFloat(Side, (box.y + box.height) - Y) };
            UIRenderContext_RenderPixelRectangle(context, Cell, DarkColor);
        }
    }
}

/* A colour of HSV at full alpha. */
static Color HsvColor(double hue, double saturation, double value)
{
    return UIColor_FromHsv((UIColorHsv){ .Hue = hue, .Saturation = saturation, .Value = value }, 255U);
}

/* The square: a grid of quads each with its corners' exact colours (the blend inside a small quad is within a step of the true one). */
static void DrawSquare(UIRenderContext* context, Rectangle box, double hue)
{
    double Cells = (double)UI_COLOR_PICKER_SQUARE_CELLS;
    for (size_t Row = 0U; Row < UI_COLOR_PICKER_SQUARE_CELLS; Row++)
    {
        float Top = box.y + RoundToPixel((float)((double)box.height * ((double)Row / Cells)));
        float Bottom = box.y + RoundToPixel((float)((double)box.height * ((double)(Row + 1U) / Cells)));
        double ValueTop = 1.0 - ((double)Row / Cells);
        double ValueBottom = 1.0 - ((double)(Row + 1U) / Cells);
        for (size_t Column = 0U; Column < UI_COLOR_PICKER_SQUARE_CELLS; Column++)
        {
            float Left = box.x + RoundToPixel((float)((double)box.width * ((double)Column / Cells)));
            float Right = box.x + RoundToPixel((float)((double)box.width * ((double)(Column + 1U) / Cells)));
            double SaturationLeft = (double)Column / Cells;
            double SaturationRight = (double)(Column + 1U) / Cells;
            UIRenderContext_RenderPixelGradient(context, (Rectangle){ .x = Left, .y = Top, .width = Right - Left, .height = Bottom - Top },
                HsvColor(hue, SaturationLeft, ValueTop), HsvColor(hue, SaturationRight, ValueTop), HsvColor(hue, SaturationLeft, ValueBottom),
                HsvColor(hue, SaturationRight, ValueBottom));
        }
    }
}

/* The hue strip: six runs, each a straight blend, from red at the top round to red at the bottom. */
static void DrawHueStrip(UIRenderContext* context, Rectangle box)
{
    for (size_t Run = 0U; Run < HUE_RUNS; Run++)
    {
        float Top = box.y + RoundToPixel(box.height * ((float)Run / (float)HUE_RUNS));
        float Bottom = box.y + RoundToPixel(box.height * ((float)(Run + 1U) / (float)HUE_RUNS));
        Color From = HsvColor(60.0 * (double)Run, 1.0, 1.0);
        Color To = HsvColor(60.0 * (double)(Run + 1U), 1.0, 1.0);
        UIRenderContext_RenderPixelGradient(context, (Rectangle){ .x = box.x, .y = Top, .width = box.width, .height = Bottom - Top }, From, From, To, To);
    }
}

static Error Area_Render(void* self, UIRenderContext* context)
{
    ColorArea* Area = self;
    Rectangle Pixels = UIRenderContext_GetPixelBox(context);
    Widget* Picker = NULL;
    UIColorPickerCapability* Picked = NULL;
    if (UIElement_IsBoxEmpty(Pixels) || !TryGetPicker(&Area->Base, Area->PickerId, &Picker, &Picked))
    {
        return Error_CreateSuccess();
    }
    const UIThemeStyle* Style = GetStyle(&Area->Look, &Area->Base);
    float Scale = Widget_GetEffectiveScale(&Area->Base);
    Rectangle Marker = { .x = 0.0f, .y = 0.0f, .width = 0.0f, .height = 0.0f };
    if (Area->IsSquare)
    {
        DrawSquare(context, Pixels, Picked->_hsv.Hue);
        float Side = RoundToPixel(MARKER_SIZE * Scale);
        Marker = (Rectangle){ .x = Pixels.x + RoundToPixel((float)Picked->_hsv.Saturation * Pixels.width) - RoundToPixel(Side * 0.5f),
            .y = Pixels.y + RoundToPixel((float)(1.0 - Picked->_hsv.Value) * Pixels.height) - RoundToPixel(Side * 0.5f), .width = Side, .height = Side };
    }
    else
    {
        float Share = 0.0f;
        if (Area->Axis == StripAxis_Hue)
        {
            DrawHueStrip(context, Pixels);
            Share = (float)(Picked->_hsv.Hue / 360.0);
        }
        else
        {
            DrawChecker(context, Style, Pixels, Scale);
            Color Opaque = Picked->_color;
            Opaque.a = 255U;
            Color Clear = Opaque;
            Clear.a = 0U;
            UIRenderContext_RenderPixelGradient(context, Pixels, Opaque, Opaque, Clear, Clear);
            Share = 1.0f - GetAlphaShare(Picked);
        }
        float Thickness = Math_MaxFloat(1.0f, RoundToPixel(STRIP_MARKER_THICKNESS * Scale));
        Marker = (Rectangle){ .x = Pixels.x, .y = Pixels.y + RoundToPixel(Share * Pixels.height) - RoundToPixel(Thickness * 0.5f), .width = Pixels.width,
            .height = Thickness };
    }
    Error Result = UIElementLook_DrawBox(&Area->Look, Style, UIColorPickerPart_Frame, context, Pixels, Scale);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    RenderContext* Render = UIRenderContext_GetRenderContext(context);
    RenderContext_PushClip(Render, Pixels);
    const UIBoxStyle* MarkerBox = UIThemeStyle_GetBox(Style, Area->IsSquare ? UIColorPickerPart_Marker : UIColorPickerPart_StripMarker);
    if (MarkerBox == NULL)
    {
        Result = UIBoxStyle_DrawFallback(context, Marker, Scale);
    }
    else
    {
        UIBoxPaint Paint = UIBoxStyle_Resolve(MarkerBox, UIElementState_Normal);
        Result = UIBoxStyle_Draw(MarkerBox, context, Marker, &Paint, Scale);
    }
    RenderContext_PopClip(Render);
    return Result;
}


// Static functions: making the popup.
static Error CreatePart(UIScreen* screen, UIKit* kit, uint64_t typeId, void* args, uint64_t parentId, uint64_t* outId)
{
    *outId = WIDGET_ID_INVALID;
    Widget* Made = NULL;
    Error Result = UIWidgetFactory_CreateWidget(kit->_factory, screen, typeId, args, &Made);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    uint64_t Id = Widget_GetId(Made);
    Result = Widget_InitializeWidget(Made);
    Widget* Parent = NULL;
    if ((Result.Code == ErrorCode_Success) && (parentId != WIDGET_ID_INVALID))
    {
        Result = (UIScreen_TryGetWidgetById(screen, parentId, &Parent) && UIScreen_TryGetWidgetById(screen, Id, &Made))
            ? Widget_AddSubWidget(Parent, Made) : Error_Construct2(ErrorCode_InvalidOperation, "UIColorPicker: a part's parent went.");
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, Id, &Made))
        {
            Error Cleanup = Widget_Destroy(Made);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    *outId = Id;
    return Error_CreateSuccess();
}

/* One labelled field of the row: a channel's whole number, or a share with two places. */
static Error AddField(UIScreen* screen, UIKit* kit, ColorPopup* popup, const unsigned char* name, Channel channel, double value, float width)
{
    UILabelledOptions Labelled = UILabelledOptions_Create(name);
    Labelled.Element.Hint = UIStackHint_Content();
    uint64_t LabelledId = WIDGET_ID_INVALID;
    Error Result = UIKit_CreateLabelled(kit, screen, popup->FieldRowId, &Labelled, &LabelledId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    bool IsShare = (channel == Channel_Brightness) || (channel == Channel_Opacity);
    UIPropertyBoxOptions Box = UIPropertyBoxOptions_CreateRange(0.0, IsShare ? 1.0 : 255.0, value);
    Box.Value.Settings.IsInteger = !IsShare;
    Box.Precision = IsShare ? FRACTION_PRECISION : 0;
    Box.Element.Hint = UIStackHint_Fixed(width);
    uint64_t FieldId = WIDGET_ID_INVALID;
    Result = UIKit_CreatePropertyBox(kit, screen, LabelledId, &Box, &FieldId);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    size_t Index = popup->FieldCount;
    popup->FieldIds[Index] = FieldId;
    popup->FieldChannels[Index] = channel;
    popup->FieldCount++;
    UserData Data = MakeFieldData(popup->PickerId, (uint32_t)channel);
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    return UIScreen_Subscribe(screen, FieldId, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnFieldEvent, &Data, &Subscription);
}

/* Makes the popup's parts: the square, the strips, the row of fields, the hex field. Re-resolves the popup after each call that runs hooks. */
static Error BuildPopup(UIScreen* screen, UIKit* kit, uint64_t popupId, const UIColorPickerCapability* picker, const unsigned char* variant)
{
    Widget* Found = NULL;
    if (!UIScreen_TryGetWidgetById(screen, popupId, &Found))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIColorPicker: the popup went while it was made.");
    }
    ColorPopup* Popup = (ColorPopup*)(void*)Found;
    PartArgs Square = { .PickerId = Popup->PickerId, .Axis = StripAxis_Hue, .Variant = variant };
    Error Result = CreatePart(screen, kit, kit->_colorSquareType, &Square, popupId, &Popup->SquareId);
    if (Result.Code == ErrorCode_Success)
    {
        Result = CreatePart(screen, kit, kit->_colorStripType, &Square, popupId, &Popup->HueId);
    }
    PartArgs Alpha = { .PickerId = Popup->PickerId, .Axis = StripAxis_Alpha, .Variant = variant };
    if ((Result.Code == ErrorCode_Success) && picker->_hasAlpha)
    {
        Result = CreatePart(screen, kit, kit->_colorStripType, &Alpha, popupId, &Popup->AlphaId);
    }
    const UIThemeStyle* Style = UITheme_FindStyle(UIScreen_GetTheme(screen), UIElementKind_ColorPicker, variant);
    float Spacing = UIThemeStyle_GetNumber(Style, UIElementKind_ColorPicker, UIColorPickerPart_Spacing);
    float FieldWidth = UIThemeStyle_GetNumber(Style, UIElementKind_ColorPicker, UIColorPickerPart_FieldWidth);
    UIStackOptions Row = UIStackOptions_Create(UIStackDirection_Row);
    Row.Layout.Spacing = Spacing;
    Row.Element.HasPlacement = true;
    Row.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateStack(kit, screen, popupId, &Row, &Popup->FieldRowId);
    }
    bool IsRender = picker->_kind == UIColorKind_RenderColor;
    const struct { const unsigned char* Name; Channel Channel; bool IsShown; } Fields[] =
    {
        { u8"R", Channel_Red, true }, { u8"G", Channel_Green, true }, { u8"B", Channel_Blue, true },
        { u8"A", Channel_Alpha, !IsRender && picker->_hasAlpha }, { u8"Br", Channel_Brightness, IsRender },
        { u8"Op", Channel_Opacity, IsRender && picker->_hasAlpha }
    };
    for (size_t Index = 0U; (Index < (sizeof(Fields) / sizeof(Fields[0]))) && (Result.Code == ErrorCode_Success); Index++)
    {
        if (Fields[Index].IsShown)
        {
            Result = AddField(screen, kit, Popup, Fields[Index].Name, Fields[Index].Channel, GetChannelValue(picker, Fields[Index].Channel), FieldWidth);
        }
    }
    UILabelledOptions HexRow = UILabelledOptions_Create(u8"Hex");
    HexRow.Element.HasPlacement = true;
    HexRow.Element.Placement = UIPlacement_FromTopLeft(0.0f, 0.0f, 0.0f, 0.0f);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateLabelled(kit, screen, popupId, &HexRow, &Popup->HexRowId);
    }
    UITextBoxOptions Hex = UITextBoxOptions_Create();
    Hex.Limits.MaxBytes = UI_COLOR_HEX_BYTES + 8U; // Room for spaces around the colour; what reads as none is refused at the end.
    Hex.Element.Hint = UIStackHint_Fill(1.0f);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIKit_CreateTextBox(kit, screen, Popup->HexRowId, &Hex, &Popup->HexId);
    }
    if (Result.Code == ErrorCode_Success)
    {
        UserData Data = MakeFieldData(Popup->PickerId, (uint32_t)Channel_Hex);
        uint64_t Subscription = UI_SCREEN_ID_INVALID;
        Result = UIScreen_Subscribe(screen, Popup->HexId, UI_EVENT_KIND_MASK(UIEventKind_Widget), OnFieldEvent, &Data, &Subscription);
    }
    return (Result.Code == ErrorCode_Success) ? SyncFields(Popup, picker) : Result;
}

/* Opens the popup below the swatch, the focus entered in the hex field; the change starts. Nothing when it is open or disabled. */
static Error OpenPopup(UIScreen* screen, uint64_t pickerId)
{
    Widget* Picker = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, pickerId, &Picker, &Self);
    if ((Result.Code != ErrorCode_Success) || !Self->_isEnabled || UIScreen_IsPopupOpen(screen, Self->_popupId))
    {
        return Result;
    }
    UIKit* Kit = UIKit_FromScreen(screen);
    const UIElementLook* Look = Widget_GetCapability(Picker, UIKit_GetCapabilityId(Kit, UIElementCapability_Look));
    const unsigned char* Variant = ((Look != NULL) && (Look->_variant[0] != 0U)) ? Look->_variant : NULL;
    PopupArgs Arguments = { .Kit = Kit, .PickerId = pickerId, .Variant = Variant };
    uint64_t PopupId = WIDGET_ID_INVALID;
    Result = CreatePart(screen, Kit, Kit->_colorPopupType, &Arguments, WIDGET_ID_INVALID, &PopupId);
    if ((Result.Code == ErrorCode_Success) && (ResolvePicker(screen, pickerId, &Picker, &Self).Code == ErrorCode_Success))
    {
        Result = BuildPopup(screen, Kit, PopupId, Self, Variant);
    }
    Widget* Popup = NULL;
    uint64_t HexId = WIDGET_ID_INVALID;
    if ((Result.Code == ErrorCode_Success) && UIScreen_TryGetWidgetById(screen, PopupId, &Popup))
    {
        HexId = ((ColorPopup*)(void*)Popup)->HexId;
        UIPopupOptions Options = UIPopupOptions_Create();
        Options.AnchorWidgetId = pickerId;
        Options.Side = UIPopupSide_Below;
        Options.Flips = true;
        Options.OwnerId = pickerId;
        Options.IsOwnerInside = true;
        Options.FocusId = HexId;
        Result = UIScreen_OpenPopup(screen, PopupId, &Options);
    }
    if (Result.Code != ErrorCode_Success)
    {
        if (UIScreen_TryGetWidgetById(screen, PopupId, &Popup) && !UIScreen_IsPopupOpen(screen, PopupId))
        {
            Error Cleanup = Widget_Destroy(Popup);
            Error_Deconstruct(&Cleanup);
        }
        return Result;
    }
    Result = ResolvePicker(screen, pickerId, &Picker, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        Self->_popupId = PopupId;
        BeginChange(Self);
    }
    Error_Deconstruct(&Result);
    return Error_CreateSuccess();
}


// Fields.
static const UIKindCapability PickerCapabilities[] =
{
    { .Capability = UIElementCapability_ColorPicker, .Resolve = ResolvePickerCapability }
};

static const UIKindBehaviour PickerBehaviour =
{
    .Kind = UIElementKind_ColorPicker,
    .Capabilities = PickerCapabilities,
    .CapabilityCount = sizeof(PickerCapabilities) / sizeof(PickerCapabilities[0]),
    .BaseSize = sizeof(UIColorPickerLookBase),
    .IsFocusable = true,
    .NeedsEntering = false,
    .EntersItself = false,
    .Construct = Behaviour_Construct,
    .Update = Behaviour_Update,
    .OnHoverStart = Behaviour_OnHoverStart,
    .OnMouseInput = Behaviour_OnMouseInput,
    .OnPressLost = Behaviour_OnPressLost,
    .OnKeyboardInput = Behaviour_OnKeyboardInput
};

static const WidgetVTable PopupVTable =
{
    .Update = Popup_Update,
    .Layout = Popup_Layout,
    .Render = Popup_Render,
    .OnKeyboardInput = Popup_OnKeyboardInput,
    .Measure = Popup_Measure
};

static const WidgetVTable AreaVTable =
{
    .Update = Area_Update,
    .Render = Area_Render,
    .OnMouseInput = Area_OnMouseInput,
    .OnPressLost = Area_OnPressLost,
    .OnKeyboardInput = Area_OnKeyboardInput
};


// Internal functions (declared in UIKitInternal.h).
const UIKindBehaviour* UIColorPicker_GetKindBehaviour(void)
{
    return &PickerBehaviour;
}


// Public functions: the conversions.
UIColorPickerOptions UIColorPickerOptions_Create(void)
{
    return (UIColorPickerOptions){ .Element = UIElementOptions_Create(), .Kind = UIColorKind_Color, .Color = WHITE, .RenderColor = RenderColor_White(),
        .HasAlpha = true, .IsMixed = false, .IsEnabled = true };
}

UIColorHsv UIColorHsv_FromRgb(double red, double green, double blue)
{
    double Red = Clamp01(red);
    double Green = Clamp01(green);
    double Blue = Clamp01(blue);
    double Highest = Math_MaxDouble(Red, Math_MaxDouble(Green, Blue));
    double Lowest = Math_MinDouble(Red, Math_MinDouble(Green, Blue));
    double Delta = Highest - Lowest;
    UIColorHsv Hsv = { .Hue = 0.0, .Saturation = (Highest > 0.0) ? (Delta / Highest) : 0.0, .Value = Highest };
    if (Delta <= 0.0)
    {
        return Hsv;
    }
    double Hue = 0.0;
    if (Highest == Red)
    {
        Hue = (Green - Blue) / Delta;
    }
    else if (Highest == Green)
    {
        Hue = ((Blue - Red) / Delta) + 2.0;
    }
    else
    {
        Hue = ((Red - Green) / Delta) + 4.0;
    }
    Hue *= 60.0;
    Hsv.Hue = (Hue < 0.0) ? (Hue + 360.0) : ((Hue >= 360.0) ? (Hue - 360.0) : Hue);
    return Hsv;
}

void UIColorHsv_ToRgb(UIColorHsv hsv, double* outRed, double* outGreen, double* outBlue)
{
    double Hue = (Math_IsNaNDouble(hsv.Hue) || Math_IsInfinityDouble(hsv.Hue)) ? 0.0 : (hsv.Hue - (360.0 * Math_FloorDouble(hsv.Hue / 360.0)));
    double Saturation = Clamp01(hsv.Saturation);
    double Value = Clamp01(hsv.Value);
    double Chroma = Value * Saturation;
    double Sector = Hue / 60.0;
    double Middle = Chroma * (1.0 - Math_AbsDouble(Math_RemainderDouble(Sector, 2.0) - 1.0));
    double Red = 0.0;
    double Green = 0.0;
    double Blue = 0.0;
    switch ((int32_t)Math_FloorDouble(Sector))
    {
        case 0:
            Red = Chroma;
            Green = Middle;
            break;
        case 1:
            Red = Middle;
            Green = Chroma;
            break;
        case 2:
            Green = Chroma;
            Blue = Middle;
            break;
        case 3:
            Green = Middle;
            Blue = Chroma;
            break;
        case 4:
            Red = Middle;
            Blue = Chroma;
            break;
        default:
            Red = Chroma;
            Blue = Middle;
            break;
    }
    double Lift = Value - Chroma;
    *outRed = Red + Lift;
    *outGreen = Green + Lift;
    *outBlue = Blue + Lift;
}

UIColorHsv UIColor_ToHsv(Color color)
{
    return UIColorHsv_FromRgb((double)color.r / 255.0, (double)color.g / 255.0, (double)color.b / 255.0);
}

Color UIColor_FromHsv(UIColorHsv hsv, unsigned char alpha)
{
    double Red = 0.0;
    double Green = 0.0;
    double Blue = 0.0;
    UIColorHsv_ToRgb(hsv, &Red, &Green, &Blue);
    return (Color){ .r = ToByte(Red), .g = ToByte(Green), .b = ToByte(Blue), .a = alpha };
}

bool UIColor_TryParseHex(const unsigned char* text, size_t length, Color* outColor, bool* outHasAlpha)
{
    size_t Start = 0U;
    size_t End = length;
    while ((Start < End) && ((text[Start] == (unsigned char)' ') || (text[Start] == (unsigned char)'\t')))
    {
        Start++;
    }
    while ((End > Start) && ((text[End - 1U] == (unsigned char)' ') || (text[End - 1U] == (unsigned char)'\t')))
    {
        End--;
    }
    if ((Start < End) && (text[Start] == (unsigned char)'#'))
    {
        Start++;
    }
    size_t Digits = End - Start;
    if ((Digits != 6U) && (Digits != 8U))
    {
        return false;
    }
    unsigned char Channels[4] = { 0U, 0U, 0U, 255U };
    for (size_t Index = 0U; Index < (Digits / 2U); Index++)
    {
        int32_t High = HexDigit(text[Start + (2U * Index)]);
        int32_t Low = HexDigit(text[Start + (2U * Index) + 1U]);
        if ((High < 0) || (Low < 0))
        {
            return false;
        }
        Channels[Index] = (unsigned char)((High * 16) + Low);
    }
    *outColor = (Color){ .r = Channels[0], .g = Channels[1], .b = Channels[2], .a = Channels[3] };
    if (outHasAlpha != NULL)
    {
        *outHasAlpha = Digits == 8U;
    }
    return true;
}

void UIColor_WriteHex(Color color, bool includesAlpha, unsigned char* outText)
{
    static const unsigned char Digits[] = u8"0123456789ABCDEF";
    const unsigned char Channels[4] = { color.r, color.g, color.b, color.a };
    size_t Count = includesAlpha ? 4U : 3U;
    outText[0] = (unsigned char)'#';
    for (size_t Index = 0U; Index < Count; Index++)
    {
        outText[1U + (2U * Index)] = Digits[Channels[Index] >> 4U];
        outText[2U + (2U * Index)] = Digits[Channels[Index] & 0x0FU];
    }
    outText[1U + (2U * Count)] = 0U;
}


// Public functions: calling code.
Error UIColorPicker_SetColor(UIScreen* screen, uint64_t widgetId, Color color)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_kind == UIColorKind_RenderColor)
    {
        Self->_opacity = (float)color.a / 255.0f;
        color.a = Self->_color.a;
    }
    Self->_color = color;
    Self->_isMixed = false;
    SyncHsv(Self);
    Self->_revision++;
    return Error_CreateSuccess();
}

Error UIColorPicker_GetColor(UIScreen* screen, uint64_t widgetId, Color* outColor)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    *outColor = Self->_color;
    if (Self->_kind == UIColorKind_RenderColor)
    {
        outColor->a = ToByte((double)Self->_opacity);
    }
    return Error_CreateSuccess();
}

Error UIColorPicker_SetRenderColor(UIScreen* screen, uint64_t widgetId, RenderColor color)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_kind == UIColorKind_RenderColor)
    {
        Self->_color = color.Tint;
        Self->_brightness = ClampFloat01(color.Brightness);
        Self->_opacity = ClampFloat01(color.Opacity);
    }
    else
    {
        Self->_color = (Color){ .r = color.Tint.r, .g = color.Tint.g, .b = color.Tint.b, .a = ToByte((double)ClampFloat01(color.Opacity)) };
    }
    Self->_isMixed = false;
    SyncHsv(Self);
    Self->_revision++;
    return Error_CreateSuccess();
}

Error UIColorPicker_GetRenderColor(UIScreen* screen, uint64_t widgetId, RenderColor* outColor)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (Self->_kind == UIColorKind_RenderColor)
    {
        *outColor = (RenderColor){ .Tint = Self->_color, .Brightness = Self->_brightness, .Opacity = Self->_opacity };
    }
    else
    {
        *outColor = (RenderColor){ .Tint = Self->_color, .Brightness = 1.0f, .Opacity = (float)Self->_color.a / 255.0f };
    }
    return Error_CreateSuccess();
}

Error UIColorPicker_SetMixed(UIScreen* screen, uint64_t widgetId, bool isMixed)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        Self->_isMixed = isMixed;
        Self->_revision++;
    }
    return Result;
}

Error UIColorPicker_IsMixed(UIScreen* screen, uint64_t widgetId, bool* outIsMixed)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    if (Result.Code == ErrorCode_Success)
    {
        *outIsMixed = Self->_isMixed;
    }
    return Result;
}

Error UIColorPicker_Open(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    return (Result.Code == ErrorCode_Success) ? OpenPopup(screen, widgetId) : Result;
}

Error UIColorPicker_Close(UIScreen* screen, uint64_t widgetId)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    return (Result.Code == ErrorCode_Success) ? ClosePopup(screen, Self) : Result;
}

Error UIColorPicker_GetParts(UIScreen* screen, uint64_t widgetId, UIColorPickerParts* outParts)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Memory_Zero(outParts, sizeof(*outParts));
    ColorPopup* Popup = UIScreen_IsPopupOpen(screen, Self->_popupId) ? GetPopup(screen, Self) : NULL;
    if (Popup == NULL)
    {
        return Error_CreateSuccess();
    }
    outParts->PopupId = Widget_GetId(&Popup->Base);
    outParts->SquareId = Popup->SquareId;
    outParts->HueId = Popup->HueId;
    outParts->AlphaId = Popup->AlphaId;
    outParts->FieldCount = Popup->FieldCount;
    for (size_t Index = 0U; Index < Popup->FieldCount; Index++)
    {
        outParts->FieldIds[Index] = Popup->FieldIds[Index];
    }
    outParts->HexId = Popup->HexId;
    return Error_CreateSuccess();
}

Error UIColorPicker_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled)
{
    Widget* Target = NULL;
    UIColorPickerCapability* Self = NULL;
    Error Result = ResolvePicker(screen, widgetId, &Target, &Self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Self->_isEnabled = isEnabled;
    if (!isEnabled)
    {
        UIPressable_Cancel(&Self->_press);
        Result = ClosePopup(screen, Self);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    Result = ResolvePicker(screen, widgetId, &Target, &Self);
    return (Result.Code == ErrorCode_Success) ? Widget_SetIsFocusable(Target, isEnabled) : Result;
}


// Public functions: behaviour helpers.
UIElementState UIColorPickerCapability_GetState(const UIColorPickerCapability* self, const Widget* widget)
{
    bool IsOpen = UIScreen_IsPopupOpen(Widget_GetScreen(widget), self->_popupId);
    return UIElementState_Choose(self->_isEnabled, UIPressable_IsPressed(&self->_press) || IsOpen, false, false, false, Widget_IsHovered(widget),
        Widget_IsFocused(widget));
}

Color UIColorPickerCapability_GetShownColor(const UIColorPickerCapability* self)
{
    if (self->_kind == UIColorKind_RenderColor)
    {
        RenderColor Value = { .Tint = self->_color, .Brightness = self->_brightness, .Opacity = self->_opacity };
        return RenderColor_GetFinalColor(Value);
    }
    return self->_color;
}

Error UIColorPicker_RegisterTypes(UIKit* kit, UIWidgetFactory* factory)
{
    WidgetTypeDescriptor Popup = { .Name = u8"ColorPickerPopup", .StructSize = sizeof(ColorPopup), .VTable = &PopupVTable, .Constructor = Popup_Construct };
    // The square and the strips are edit elements: a press enters them (the popup keeps the focus, Enter and Escape reach it), and so does
    // Tab carrying editing on, their arrows then theirs.
    WidgetTypeDescriptor Square = { .Name = u8"ColorSquare", .StructSize = sizeof(ColorArea), .VTable = &AreaVTable, .Constructor = Square_Construct,
        .IsFocusable = true, .NeedsEntering = true };
    WidgetTypeDescriptor Strip = { .Name = u8"ColorStrip", .StructSize = sizeof(ColorArea), .VTable = &AreaVTable, .Constructor = Area_Construct,
        .IsFocusable = true, .NeedsEntering = true };
    Error Result = UIWidgetFactory_RegisterType(factory, &Popup, &kit->_colorPopupType);
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_RegisterType(factory, &Square, &kit->_colorSquareType);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Result = UIWidgetFactory_RegisterType(factory, &Strip, &kit->_colorStripType);
    }
    return Result;
}

Error UIColorPicker_Attach(UIScreen* screen, uint64_t widgetId)
{
    uint64_t Subscription = UI_SCREEN_ID_INVALID;
    return UIScreen_Subscribe(screen, widgetId, UI_EVENT_KIND_MASK(UIEventKind_PopupClosed), OnPopupClosed, NULL, &Subscription);
}
