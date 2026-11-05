#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Vector2 is part of the lookups' answers; the box styles are what a theme holds.
#include "raylib/raylib.h"
#include "UIBoxStyle.h"


/**
 * @file UITheme.h
 * @brief The look of the UI's elements as data: the ui_theme asset type (references/asset_structure.md, "UI themes").
 *
 * A theme names a font and a text size, a transition time for state changes, and per element kind its metrics (padding, a
 * minimum size, a thumb's size) and its parts (a slider's track, fill and thumb), each part a box style or a colour per
 * state. Named variants (a "danger" button) change only what they name. The whole UI changes at once with UIScreen_SetTheme,
 * with no element's code changing: art arrives as a file.
 *
 * A definition refuses unknown keys and checks every rule when it is read. The textures and the font it names are the
 * theme's dependencies: they stay loaded as long as it does. A texture that does not load, or whose art does not fit its
 * margins, leaves that part drawing a plain box and is logged once; the rest of the theme works.
 *
 * Elements look values up at layout and at render, through UITheme_FindStyle and the UIThemeStyle_Get* functions, and never
 * keep what they got past that: the screen's theme can change at any time. Every lookup takes a NULL style, so an element
 * on a screen without a theme simply gets the defaults.
 */


// Macros.
/** @brief The most named variants of one element kind in a theme. */
#define UI_THEME_MAX_VARIANTS ((size_t)16)
/** @brief The longest variant name, in bytes, its NUL included. */
#define UI_THEME_VARIANT_NAME_CAPACITY ((size_t)32)
/** @brief The most different textures one theme may name. */
#define UI_THEME_MAX_TEXTURES ((size_t)64)
/** @brief The most parts and metrics one element kind may have. */
#define UI_THEME_MAX_PARTS ((size_t)12)
/** @brief The largest metric a theme may give, in UI pixels. */
#define UI_THEME_MAX_METRIC 4096.0f
/** @brief The bounds of a theme's text size, in UI pixels, and its default. */
#define UI_THEME_MIN_TEXT_SIZE 4.0f
#define UI_THEME_MAX_TEXT_SIZE 256.0f
#define UI_THEME_DEFAULT_TEXT_SIZE 14.0f
/** @brief The longest transition a theme may give, in seconds. */
#define UI_THEME_MAX_TRANSITION_SECONDS 5.0f
/** @brief The bounds of a nine-slice's pixel scale. */
#define UI_THEME_MIN_PIXEL_SCALE 0.125f
#define UI_THEME_MAX_PIXEL_SCALE 16.0f
/** @brief The largest outline, in UI pixels. */
#define UI_THEME_MAX_OUTLINE 64.0f


// Types.
/** @brief A theme asset; loaded with AssetManager_LoadUITheme, read through the functions below. */
typedef struct UIThemeStruct UITheme;
/** @brief The values of one element kind or of one of its variants; borrowed from a theme. */
typedef struct UIThemeStyleStruct UIThemeStyle;

/**
 * @brief The kinds of element a theme describes, one block each under its "elements".
 */
typedef enum UIElementKindEnum
{
    /** @brief A background and padding for what it holds. */
    UIElementKind_Panel,
    /** @brief Text. */
    UIElementKind_Label,
    /** @brief A button. */
    UIElementKind_Button,
    /** @brief A checkbox: on, off or mixed. */
    UIElementKind_Toggle,
    /** @brief A value in a range. */
    UIElementKind_Slider,
    /** @brief The number of kinds. */
    UIElementKind_Count
} UIElementKind;

/** @brief A panel's values in a theme. */
typedef enum UIPanelPartEnum
{
    /** @brief "padding": horizontal and vertical, UI pixels. */
    UIPanelPart_Padding,
    /** @brief "spacing": between its children, UI pixels. */
    UIPanelPart_Spacing,
    /** @brief "min_size": UI pixels. */
    UIPanelPart_MinSize,
    /** @brief "background": a box. */
    UIPanelPart_Background,
    /** @brief The number of a panel's values. */
    UIPanelPart_Count
} UIPanelPart;

/** @brief A label's values in a theme. */
typedef enum UILabelPartEnum
{
    /** @brief "text": a colour per state. */
    UILabelPart_Text,
    /** @brief The number of a label's values. */
    UILabelPart_Count
} UILabelPart;

/** @brief A button's values in a theme. */
typedef enum UIButtonPartEnum
{
    /** @brief "padding": horizontal and vertical around its text, UI pixels. */
    UIButtonPart_Padding,
    /** @brief "min_size": UI pixels. */
    UIButtonPart_MinSize,
    /** @brief "background": a box. */
    UIButtonPart_Background,
    /** @brief "text": a colour per state. */
    UIButtonPart_Text,
    /** @brief The number of a button's values. */
    UIButtonPart_Count
} UIButtonPart;

/** @brief A toggle's values in a theme. */
typedef enum UITogglePartEnum
{
    /** @brief "box_size": the checkbox's size, UI pixels. */
    UITogglePart_BoxSize,
    /** @brief "spacing": between the box and the text, UI pixels. */
    UITogglePart_Spacing,
    /** @brief "min_size": UI pixels. */
    UITogglePart_MinSize,
    /** @brief "box": a box. */
    UITogglePart_Box,
    /** @brief "check": a box, drawn inside it while on, as a bar while mixed. */
    UITogglePart_Check,
    /** @brief "text": a colour per state. */
    UITogglePart_Text,
    /** @brief The number of a toggle's values. */
    UITogglePart_Count
} UITogglePart;

/** @brief A slider's values in a theme. */
typedef enum UISliderPartEnum
{
    /** @brief "min_size": UI pixels. */
    UISliderPart_MinSize,
    /** @brief "thumb_size": UI pixels. */
    UISliderPart_ThumbSize,
    /** @brief "track_thickness": UI pixels. */
    UISliderPart_TrackThickness,
    /** @brief "track": a box. */
    UISliderPart_Track,
    /** @brief "fill": a box, the track from its start to the thumb. */
    UISliderPart_Fill,
    /** @brief "thumb": a box. */
    UISliderPart_Thumb,
    /** @brief The number of a slider's values. */
    UISliderPart_Count
} UISliderPart;

/**
 * @brief What a value of an element kind is.
 */
typedef enum UIThemePartKindEnum
{
    /** @brief A box style. */
    UIThemePartKind_Box,
    /** @brief A colour per state. */
    UIThemePartKind_Colors,
    /** @brief Two numbers (an array or a compound), each within the metric's bounds. */
    UIThemePartKind_Vector,
    /** @brief One number within the metric's bounds. */
    UIThemePartKind_Number
} UIThemePartKind;

/**
 * @brief One value an element kind has in a theme: its key, what it is and, for a metric, the value a theme that leaves it
 *        out gets. Static data.
 */
typedef struct UIThemePartDescriptorStruct
{
    /** @brief The key in the element's block. */
    const unsigned char* Key;
    /** @brief What it is. */
    UIThemePartKind Kind;
    /** @brief The default of a UIThemePartKind_Vector. */
    Vector2 DefaultVector;
    /** @brief The default of a UIThemePartKind_Number. */
    float DefaultNumber;
} UIThemePartDescriptor;


// The element kinds.
/**
 * @brief Returns an element kind's key in a theme's "elements".
 * @param kind The kind.
 * @returns The static name, or NULL for a value out of range.
 */
const unsigned char* UIElementKind_GetName(UIElementKind kind);

/**
 * @brief Returns how many values an element kind has.
 * @param kind The kind.
 * @returns The count; 0 for a value out of range.
 */
size_t UIElementKind_GetPartCount(UIElementKind kind);

/**
 * @brief Returns the description of one of an element kind's values.
 * @param kind The kind.
 * @param part The value's index: one of that kind's part enum.
 * @returns The static description, or NULL when out of range.
 */
const UIThemePartDescriptor* UIElementKind_GetPart(UIElementKind kind, int32_t part);


// The theme.
/**
 * @brief Returns the theme's asset name.
 * @param self The theme; must not be NULL.
 * @returns The name; valid while the theme is.
 */
const unsigned char* UITheme_GetName(const UITheme* self);

/**
 * @brief Returns the theme's font's asset name.
 * @param self The theme, or NULL.
 * @returns The name, or NULL for no theme, a theme that names none or a font that did not load: text then draws in raylib's
 *          built-in font. Valid while the theme is.
 */
const unsigned char* UITheme_GetFontName(const UITheme* self);

/**
 * @brief Returns the theme's text size.
 * @param self The theme, or NULL.
 * @returns The line height in UI pixels; UI_THEME_DEFAULT_TEXT_SIZE for no theme.
 */
float UITheme_GetTextSize(const UITheme* self);

/**
 * @brief Returns how long the elements' colours take to fade from state to state.
 * @param self The theme, or NULL.
 * @returns The time in seconds; 0 (at once) for no theme.
 */
float UITheme_GetTransitionSeconds(const UITheme* self);

/**
 * @brief Finds the values of an element kind, or of one of its variants.
 * @param self The theme, or NULL.
 * @param kind The kind.
 * @param variant The variant's name, or NULL or empty for the kind's own values. An unknown variant gets the kind's own.
 * @returns The style, or NULL when there is no theme or it leaves the kind out. Valid while the theme is.
 */
const UIThemeStyle* UITheme_FindStyle(const UITheme* self, UIElementKind kind, const unsigned char* variant);

/**
 * @brief Returns a box part of a style.
 * @param self The style, or NULL.
 * @param part The part's index; a part that is not a box counts as left out.
 * @returns The box, or NULL when left out.
 */
const UIBoxStyle* UIThemeStyle_GetBox(const UIThemeStyle* self, int32_t part);

/**
 * @brief Returns a colours part of a style.
 * @param self The style, or NULL.
 * @param part The part's index; a part that is not colours counts as left out.
 * @returns The colours, or NULL when left out.
 */
const UIStateColors* UIThemeStyle_GetColors(const UIThemeStyle* self, int32_t part);

/**
 * @brief Returns a vector metric of a style.
 * @param self The style, or NULL.
 * @param kind The style's element kind (for the default).
 * @param part The part's index.
 * @returns The metric, or the kind's default when left out or @p self is NULL; zero when @p part is no vector.
 */
Vector2 UIThemeStyle_GetVector(const UIThemeStyle* self, UIElementKind kind, int32_t part);

/**
 * @brief Returns a number metric of a style.
 * @param self The style, or NULL.
 * @param kind The style's element kind (for the default).
 * @param part The part's index.
 * @returns The metric, or the kind's default when left out or @p self is NULL; zero when @p part is no number.
 */
float UIThemeStyle_GetNumber(const UIThemeStyle* self, UIElementKind kind, int32_t part);
