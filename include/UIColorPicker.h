#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
// Color is the value a picker holds; RenderColor the other value it can hold (the renderer's).
#include "raylib/raylib.h"
#include "Renderer.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIColorPicker.h
 * @brief The colour picker (kind color_picker): a swatch showing a colour, and a popup that picks one: a square of saturation (across) and
 *        value (up), a strip of hue, a strip of alpha, a field per channel and a hex field.
 *
 * THE VALUE is a Color or a RenderColor, as the picker was made (UIColorKind): a Color's four channels; or a RenderColor's tint, its
 * brightness and its opacity (the tint's own alpha is kept as given and never edited). Without HasAlpha the alpha (or the opacity) is kept as
 * it is and not shown. Calling code reads and sets it by id (UIColorPicker_SetColor, _GetColor, _SetRenderColor, _GetRenderColor); several
 * objects whose colours differ show as mixed (UIColorPicker_SetMixed: a hyphen on the swatch) until an edit makes them one.
 *
 * THE SWATCH is a press element: its click mode (a press let go on it, by default), Enter or Space opens the popup; it draws the colour over
 * a checkerboard where it is not opaque. Disabled, it swallows presses and is not focusable.
 *
 * THE POPUP opens below the swatch (above with no room), with the focus in the hex field, entered: typing a hex at once works. Dragging on
 * the square or a strip moves the colour, as do the fields (whole numbers 0 to 255 for the channels, a brightness and an opacity from 0 to 1)
 * and the hex field ("#rrggbb" or "#rrggbbaa", the # optional, refused and put back when it reads as no colour). The square and the strips
 * are drawn from quads with a colour at each corner (no shader of their own); while the saturation or the value is 0 the hue is kept, so the
 * colour can be dragged out of grey or black again. Changed comes with each move; Committed when the popup closes with the colour other than
 * it opened with (a press outside, Enter, code); Escape puts back the colour it opened with (Cancelled) and closes it. Enter and Escape act
 * in one press from inside an entered field (the popup leaves the field itself first, keeping or cancelling it).
 *
 * The conversions are pure (UIColorHsv_*, UIColor_*): a Color through its HSV and back is the same Color, every one of them.
 */


// Forward declarations.
/** @brief The widget factory; full type in UIWidgetFactory.h. */
typedef struct UIWidgetFactoryStruct UIWidgetFactory;


// Macros.
/** @brief How many quads a side of the square is drawn with: a gradient across a few pixels each, so the triangles' blend shows no seam. */
#define UI_COLOR_PICKER_SQUARE_CELLS ((size_t)8)
/** @brief How many fields a picker's popup has at most: four channels (a RenderColor's three, its brightness and its opacity: five). */
#define UI_COLOR_PICKER_MAX_FIELDS ((size_t)5)
/** @brief The bytes a hex colour's text takes at most, "#rrggbbaa" and its NUL. */
#define UI_COLOR_HEX_BYTES ((size_t)10)


// Types.
/**
 * @brief What a picker's value is.
 */
typedef enum UIColorKindEnum
{
    /** @brief A Color: red, green, blue and alpha, each 0 to 255. */
    UIColorKind_Color,
    /** @brief A RenderColor: a tint's red, green and blue, a brightness and an opacity. */
    UIColorKind_RenderColor
} UIColorKind;

/**
 * @brief A colour as hue, saturation and value. A plain value.
 */
typedef struct UIColorHsvStruct
{
    /** @brief The hue in degrees, within [0, 360). */
    double Hue;
    /** @brief The saturation, within [0, 1]. */
    double Saturation;
    /** @brief The value, within [0, 1]. */
    double Value;
} UIColorHsv;

/**
 * @brief What a colour picker is created with (UIKit_CreateColorPicker). A plain value; build one with UIColorPickerOptions_Create.
 */
typedef struct UIColorPickerOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief What its value is. */
    UIColorKind Kind;
    /** @brief Its value, for a Color picker. */
    Color Color;
    /** @brief Its value, for a RenderColor picker; brightness and opacity within [0, 1]. */
    RenderColor RenderColor;
    /** @brief Whether the alpha (a Color's) or the opacity (a RenderColor's) is edited and shown; otherwise kept as it is. */
    bool HasAlpha;
    /** @brief Whether several colours differ. */
    bool IsMixed;
    /** @brief Whether it takes input. */
    bool IsEnabled;
} UIColorPickerOptions;

/**
 * @brief What a colour picker look's Construct gets (UIKit_CreateColorPicker passes it): the kit and the options.
 */
typedef struct UIColorPickerArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIColorPickerOptions* Options;
} UIColorPickerArgs;

/**
 * @brief The widgets of an open picker's popup, for calling code that drives it (tests, a tutorial).
 */
typedef struct UIColorPickerPartsStruct
{
    /** @brief The popup's root. */
    uint64_t PopupId;
    /** @brief The square, the hue strip and the alpha strip (WIDGET_ID_INVALID without alpha). */
    uint64_t SquareId;
    uint64_t HueId;
    uint64_t AlphaId;
    /** @brief The fields (property boxes) in order: red, green, blue, then alpha for a Color, or brightness then opacity for a RenderColor;
     *  FieldCount of them. */
    uint64_t FieldIds[UI_COLOR_PICKER_MAX_FIELDS];
    size_t FieldCount;
    /** @brief The hex field (a text box). */
    uint64_t HexId;
} UIColorPickerParts;

/**
 * @brief A colour picker's capability: its value and its logical state, no look. Embedded by a look's widget type right after its Widget
 *        (UIColorPickerLookBase); its fields are read-only outside the picker's module.
 */
typedef struct UIColorPickerCapabilityStruct
{
    /** @brief The swatch's press behaviour. */
    UIPressable _press;
    /** @brief What its value is, and whether the alpha (or the opacity) is edited. */
    UIColorKind _kind;
    bool _hasAlpha;
    /** @brief The value: a Color's channels, or a RenderColor's tint (its alpha kept), brightness and opacity. */
    Color _color;
    float _brightness;
    float _opacity;
    /** @brief The colour as the popup edits it: the hue kept through grey, the saturation through black. */
    UIColorHsv _hsv;
    /** @brief Whether several colours differ. */
    bool _isMixed;
    /** @brief Whether it takes input. */
    bool _isEnabled;
    /** @brief The open popup's root, or WIDGET_ID_INVALID. */
    uint64_t _popupId;
    /** @brief Whether the popup's change is in progress, and the value it opened with. */
    bool _isChanging;
    Color _startColor;
    float _startBrightness;
    float _startOpacity;
    bool _startIsMixed;
    /** @brief Moves on with every change of the value, so the popup's fields and a look follow it. */
    uint64_t _revision;
} UIColorPickerCapability;

/**
 * @brief How every colour picker look's widget struct starts: the widget, then the capability.
 */
typedef struct UIColorPickerLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The picker's behaviour. */
    UIColorPickerCapability Picker;
} UIColorPickerLookBase;


// Helpers.
/**
 * @brief Default options: a Color picker of opaque white with alpha, enabled, not mixed.
 * @returns The options.
 */
UIColorPickerOptions UIColorPickerOptions_Create(void);


// The conversions (pure).
/**
 * @brief Converts red, green and blue in [0, 1] to hue, saturation and value; a grey (no saturation) has the hue 0.
 * @param red The red, within [0, 1] (clamped).
 * @param green The green.
 * @param blue The blue.
 * @returns The colour as HSV.
 */
UIColorHsv UIColorHsv_FromRgb(double red, double green, double blue);

/**
 * @brief Converts hue, saturation and value to red, green and blue in [0, 1].
 * @param hsv The colour; a hue outside [0, 360) wraps, a saturation or value outside [0, 1] is clamped, NaN counts as 0.
 * @param outRed [out] Receives the red. Must not be NULL.
 * @param outGreen [out] Receives the green. Must not be NULL.
 * @param outBlue [out] Receives the blue. Must not be NULL.
 */
void UIColorHsv_ToRgb(UIColorHsv hsv, double* outRed, double* outGreen, double* outBlue);

/**
 * @brief Converts a Color's red, green and blue to HSV.
 * @param color The colour (its alpha is not part of it).
 * @returns The colour as HSV.
 */
UIColorHsv UIColor_ToHsv(Color color);

/**
 * @brief Converts HSV to a Color, each channel rounded to the nearest of 0 to 255; UIColor_FromHsv(UIColor_ToHsv(c), c.a) is c.
 * @param hsv The colour.
 * @param alpha The alpha to give it.
 * @returns The Color.
 */
Color UIColor_FromHsv(UIColorHsv hsv, unsigned char alpha);

/**
 * @brief Reads a hex colour: "#rrggbb" or "#rrggbbaa", the # optional, either case, spaces around it passed over; nothing else.
 * @param text The text; must not be NULL.
 * @param length How many bytes of it.
 * @param outColor [out] Receives the colour (alpha 255 without one). Must not be NULL.
 * @param outHasAlpha [out] Receives whether it gave an alpha; may be NULL.
 * @returns true when it reads as a colour.
 */
bool UIColor_TryParseHex(const unsigned char* text, size_t length, Color* outColor, bool* outHasAlpha);

/**
 * @brief Writes a colour as "#RRGGBB", or "#RRGGBBAA" with its alpha.
 * @param color The colour.
 * @param includesAlpha Whether its alpha is written.
 * @param outText [out] Receives the NUL-terminated text; at least UI_COLOR_HEX_BYTES bytes. Must not be NULL.
 */
void UIColor_WriteHex(Color color, bool includesAlpha, unsigned char* outText);


// Calling code: by widget id.
/**
 * @brief Sets a Color picker's value by code (no event); the mixed state ends. For a RenderColor picker: the tint's red, green and blue,
 *        and the opacity from the alpha (the brightness kept).
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @param color The colour.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker.
 */
Error UIColorPicker_SetColor(UIScreen* screen, uint64_t widgetId, Color color);

/**
 * @brief Reads a picker's value as a Color: a Color picker's own, or a RenderColor picker's tint with its opacity as the alpha.
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @param outColor [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker.
 */
Error UIColorPicker_GetColor(UIScreen* screen, uint64_t widgetId, Color* outColor);

/**
 * @brief Sets a RenderColor picker's value by code (no event); the mixed state ends. For a Color picker: the tint's red, green and blue, and
 *        the alpha from the opacity (the brightness is not part of a Color).
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @param color The colour; its brightness and opacity clamped into [0, 1], NaN as 0.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker.
 */
Error UIColorPicker_SetRenderColor(UIScreen* screen, uint64_t widgetId, RenderColor color);

/**
 * @brief Reads a picker's value as a RenderColor: a RenderColor picker's own, or a Color picker's channels as the tint, brightness 1 and the
 *        alpha as the opacity.
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @param outColor [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker.
 */
Error UIColorPicker_GetRenderColor(UIScreen* screen, uint64_t widgetId, RenderColor* outColor);

/**
 * @brief Shows a picker as mixed (several colours differ) or not, by code (no event).
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @param isMixed Whether it is.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker.
 */
Error UIColorPicker_SetMixed(UIScreen* screen, uint64_t widgetId, bool isMixed);

/**
 * @brief Reads whether a picker shows as mixed.
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @param outIsMixed [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker.
 */
Error UIColorPicker_IsMixed(UIScreen* screen, uint64_t widgetId, bool* outIsMixed);

/**
 * @brief Opens a picker's popup, as Enter does; nothing happens when it is open or disabled.
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker; otherwise an error from making the
 *          popup.
 */
Error UIColorPicker_Open(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Closes a picker's popup, keeping the colour (Committed when it changed); nothing happens when it is closed.
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker; otherwise a hook's error.
 */
Error UIColorPicker_Close(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Reads the widgets of a picker's open popup.
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @param outParts [out] Receives them; every id WIDGET_ID_INVALID while closed. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker.
 */
Error UIColorPicker_GetParts(UIScreen* screen, uint64_t widgetId, UIColorPickerParts* outParts);

/**
 * @brief Enables or disables a picker. Disabling it closes its popup (keeping the colour) and takes its focus away.
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @param isEnabled Whether it takes input.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no colour picker; otherwise a hook's error.
 */
Error UIColorPicker_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);


// Behaviour helpers, for looks.
/**
 * @brief Returns the state a look shows the swatch in: disabled, pressed (held, or its popup open), hovered, focused or normal.
 * @param self The capability; must not be NULL.
 * @param widget The picker's widget; must not be NULL.
 * @returns The state.
 */
UIElementState UIColorPickerCapability_GetState(const UIColorPickerCapability* self, const Widget* widget);

/**
 * @brief Returns the colour the swatch shows: a Color's own, a RenderColor's as it draws (its tint by its brightness, its opacity as alpha).
 * @param self The capability; must not be NULL.
 * @returns The colour.
 */
Color UIColorPickerCapability_GetShownColor(const UIColorPickerCapability* self);

/**
 * @brief Registers the kit's own widget types a picker's popup is made of: the popup, the square and the strips. The kit calls it when it
 *        is constructed.
 * @param kit The kit, whose capabilities exist already; must not be NULL.
 * @param factory The kit's factory; must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIColorPicker_RegisterTypes(UIKit* kit, UIWidgetFactory* factory);

/**
 * @brief Starts listening to a picker's popup closing (to keep or put back its colour); UIKit_CreateColorPicker calls it once the picker is
 *        made.
 * @param screen The picker's screen; must not be NULL.
 * @param widgetId The picker.
 * @returns Success, or the subscription's error.
 */
Error UIColorPicker_Attach(UIScreen* screen, uint64_t widgetId);
