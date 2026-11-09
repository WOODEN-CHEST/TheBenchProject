#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
// The text box's capability is the first part of a property box's: typing goes through it.
#include "UIElement.h"
#include "UITextBox.h"
#include "UITextEdit.h"
#include "UIValue.h"
#include "UIWidget.h"


/**
 * @file UIPropertyBox.h
 * @brief The property box element (kind property_box): a value in a box that can be typed in, or dragged sideways if it is a number,
 *        Blender's field. A number holds a value piece (UIValue.h), reached by the UIValue_* functions by widget id; a string holds only
 *        text.
 *
 * SHOWING: the value with Precision digits after the point (none for a whole number) and a unit after it, or a dash while it is mixed,
 * over a fill bar by the conversion when it ShowsFill and has a drag range; a string shows its text. Showing never changes the value.
 *
 * SCRUBBING (a number): a left press holds; once the pointer has gone UI_PROPERTY_BOX_SCRUB_THRESHOLD UI pixels sideways the box scrubs:
 * the cursor hides and moves free of the window's edges (UIScreen_CapturePointer), and each move changes the value: with a drag range
 * the box's width spans it (through the conversion), without one by UIValueSettings_GetScrubRate per UI pixel; Shift slows it by the
 * FineFactor, Ctrl snaps it coarsely (UIValueSettings_SnapCoarse). A drag stops at the soft range (widened to take in a value typed past
 * it), typing goes to the hard limits. Changed while it moves, Committed at the release (unless it ended where it began); Escape or a
 * right press cancels it (Cancelled, the value at its start back). The cursor comes back where it began. A scrub does not enter the box.
 *
 * TYPING is the box's entered state (UIWidget.h, "EDIT ELEMENTS"), in a text box of its own: the same text piece, keys, mouse, clipboard
 * and undo as a one-line text box's, its undo starting empty at each visit (the value's history is the caller's). The box enters itself
 * (a press may become a scrub): a click without moving, Enter or a double click on a number, any press on a string, enters it with
 * everything selected; a number's text is the shortest that reads back to its value. While the text reads as no number the box shows
 * the invalid state. Enter keeps and leaves (a number's text is read with WRFramework's number parser, a unit after it passed over: a
 * good one clamped into the hard limits and snapped, a bad one goes back); Escape cancels and leaves; Tab and Shift+Tab keep, leave and
 * go on, typing in the next box at once (UIWidget.h). A visit that changed the value ends in Changed then Committed; text kept as it
 * began changes nothing, so a value is never changed by showing it.
 *
 * ALSO: a middle press let go on it, or the reset action (focused or under the pointer), puts the default back; copy and paste work
 * focused or under the pointer (the value's shortest text, a number read from the clipboard) and from its context menu (Reset, Copy,
 * Paste; while typing, a text box's menu). Ctrl and the wheel step a number (Shift: a fine step); a plain wheel goes up the tree, so
 * panels scroll. Disabled, it swallows presses and takes no focus.
 *
 * EVENTS carry a number's value as a double, a string's length in bytes (its text is read with UIPropertyBox_GetText).
 *
 * LOOKS (UIKit_RegisterLook: a struct starting with UIPropertyBoxLookBase) draw the box, the fill, the shown text and the unit, and while
 * typing the text box's text, selection and caret: they give the text box its geometry (UITextBoxCapability_SetGeometry) as a text box
 * look does, and read the rest here (UIPropertyBoxCapability_*).
 */


// Macros.
/** @brief How far the pointer goes sideways after a press, in UI pixels, before the press becomes a scrub. */
#define UI_PROPERTY_BOX_SCRUB_THRESHOLD 3.0f
/** @brief The longest unit a box shows after its number, in bytes. */
#define UI_PROPERTY_BOX_MAX_UNIT_BYTES ((size_t)31)
/** @brief How many digits after the point a number shows unless its options say otherwise. */
#define UI_PROPERTY_BOX_DEFAULT_PRECISION 3


// Types.
/**
 * @brief What a property box is created with (UIKit_CreatePropertyBox). A plain value; build one with UIPropertyBoxOptions_Create,
 *        UIPropertyBoxOptions_CreateRange or UIPropertyBoxOptions_CreateString.
 */
typedef struct UIPropertyBoxOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief Whether it holds a string: no value piece and no scrub; a press types at once. */
    bool IsString;
    /** @brief A number's value, its settings and its states (IsEnabled counts for a string too). */
    UIValueOptions Value;
    /** @brief Digits after the point while a number shows, within [0, UI_VALUE_MAX_PRECISION]; a whole number shows none. */
    int32_t Precision;
    /** @brief What shows after a number (" m"), UTF-8, at most UI_PROPERTY_BOX_MAX_UNIT_BYTES; NULL for none. Copied. */
    const unsigned char* Unit;
    /** @brief Whether a number with a drag range shows a bar behind it by the conversion. */
    bool ShowsFill;
    /** @brief A string's text, UTF-8 through its limits; NULL for none. Read for the call. */
    const unsigned char* Text;
    /** @brief What a string may hold (UITextEdit.h); line breaks are never allowed. A number's text keeps to UI_VALUE_MAX_TEXT_BYTES. */
    UITextEditLimits Limits;
    /** @brief Whether a string has a text to go back to (the reset action, a middle click). */
    bool HasDefaultText;
    /** @brief That text, when HasDefaultText; NULL for an empty one. Copied. */
    const unsigned char* DefaultText;
} UIPropertyBoxOptions;

/**
 * @brief What a property box look's Construct gets (UIKit_CreatePropertyBox passes it): the kit and the options.
 */
typedef struct UIPropertyBoxArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIPropertyBoxOptions* Options;
} UIPropertyBoxArgs;

/**
 * @brief A property box's capability, after the text box's: its value piece, what it shows and what a press, a scrub or typing is doing.
 *        No look; its fields are read-only outside the property box's module, the value piece's outside the value's.
 */
typedef struct UIPropertyBoxCapabilityStruct
{
    /** @brief A number's value: what UIValue_* reach (a string's resolves no value). */
    UIValueCapability Value;
    /** @brief Whether it holds a string. */
    bool _isString;
    /** @brief Digits after the point while a number shows. */
    int32_t _precision;
    /** @brief What shows after a number. */
    UITextContent _unit;
    /** @brief Whether a number with a drag range shows its fill bar. */
    bool _showsFill;
    /** @brief What shows while it is not typed in: the value at its precision, a dash while mixed, or the string. */
    UITextContent _shownText;
    /** @brief What _shownText was written from, so it is written again only when that changed. */
    bool _hasShown;
    double _shownValue;
    int32_t _shownPrecision;
    bool _shownIsMixed;
    bool _shownIsInteger;
    uint64_t _shownTextRevision;
    /** @brief A left press is held on it while it is not typed in (it may become a scrub, or a click). */
    bool _isPressed;
    /** @brief The press is scrubbing. */
    bool _isScrubbing;
    /** @brief The press's scrub was cancelled: what is left of it does nothing. */
    bool _isPressCancelled;
    /** @brief What a scrub works in: whether it has a drag range, the range, its start and its unsnapped place (a share with a range, a
     *  value without). */
    bool _scrubHasRange;
    double _scrubMinimum;
    double _scrubMaximum;
    double _scrubStartValue;
    double _scrubPlace;
    /** @brief A middle press on it is held: let go on it, it resets. */
    bool _isMiddlePressed;
    /** @brief The right press that cancelled a scrub opens no context menu at its release. */
    bool _isMenuSuppressed;
    /** @brief The typed text reads as no number (the invalid state). */
    bool _isTextInvalid;
    /** @brief The text typing began with (bytes): kept as it is, it changes nothing. */
    GenericBuffer _typingStart;
    /** @brief The box was entered by a click, so the double click it begins selects everything rather than a word. */
    bool _isEnteredByClick;
} UIPropertyBoxCapability;

/**
 * @brief How every property box look's widget struct starts: the widget, the text box typing goes through, then the property box.
 */
typedef struct UIPropertyBoxLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The text box typing goes through: its text piece, caret, selection, undo and keys. Not reachable as a text box by id. */
    UITextBoxCapability TextBox;
    /** @brief The property box's behaviour. */
    UIPropertyBoxCapability PropertyBox;
} UIPropertyBoxLookBase;


// Helpers.
/**
 * @brief Default options for a number with no limits: no step, precision UI_PROPERTY_BOX_DEFAULT_PRECISION, no unit, no fill, enabled.
 * @returns The options.
 */
UIPropertyBoxOptions UIPropertyBoxOptions_Create(void);

/**
 * @brief Options for a number in a range, with a fill bar; the rest as UIPropertyBoxOptions_Create.
 * @param minimum The lower limit.
 * @param maximum The upper limit.
 * @param value The value.
 * @returns The options.
 */
UIPropertyBoxOptions UIPropertyBoxOptions_CreateRange(double minimum, double maximum, double value);

/**
 * @brief Options for a string: the text box's default limits on one line, no default.
 * @param text The text, or NULL for none; read when the box is made.
 * @returns The options.
 */
UIPropertyBoxOptions UIPropertyBoxOptions_CreateString(const unsigned char* text);


// Calling code: by widget id (a number's value through UIValue_*).
/*
 * Each takes the box's screen (not NULL) and its widget id, and returns ErrorCode_InvalidOperation for a stale id or a widget that is no
 * property box. None posts an event.
 */
/** @brief Sets how many digits after the point a number shows, clamped into [0, UI_VALUE_MAX_PRECISION]. */
Error UIPropertyBox_SetPrecision(UIScreen* screen, uint64_t widgetId, int32_t precision);
/** @brief Sets what shows after a number (UTF-8, at most UI_PROPERTY_BOX_MAX_UNIT_BYTES; NULL for none): ErrorCode_ArgumentOutOfRange for a
 *  longer one, ErrorCode_InvalidTextEncoding for text that is not valid UTF-8, both changing nothing. */
Error UIPropertyBox_SetUnit(UIScreen* screen, uint64_t widgetId, const unsigned char* unit);
/** @brief Sets whether a number with a drag range shows its fill bar. */
Error UIPropertyBox_SetShowsFill(UIScreen* screen, uint64_t widgetId, bool showsFill);
/** @brief Enables or disables the box, a number or a string: disabled, it ends a scrub (taken back) or a visit (kept), takes no focus and
 *  swallows presses. The same as UIValue_SetIsEnabled on a number. */
Error UIPropertyBox_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);
/** @brief Replaces a string's text through its limits (ErrorCode_InvalidOperation on a number; ErrorCode_InvalidTextEncoding for text that is
 *  not valid UTF-8). During a visit it replaces what is being typed. */
Error UIPropertyBox_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text);
/** @brief Reads a string's text into @p outText (not NULL; NUL-terminated, valid until it changes) and its length in bytes into @p outLength
 *  (may be NULL). On a number, the text being typed during a visit, else an empty text. */
Error UIPropertyBox_GetText(UIScreen* screen, uint64_t widgetId, const unsigned char** outText, size_t* outLength);
/** @brief Changes a string's limits (not NULL, valid; line breaks stay refused): its text is cut to fit (ErrorCode_InvalidOperation on a
 *  number; ErrorCode_ArgumentOutOfRange for limits that are not valid). */
Error UIPropertyBox_SetTextLimits(UIScreen* screen, uint64_t widgetId, const UITextEditLimits* limits);
/** @brief Sets the text a string goes back to on the reset action and a middle click, or takes it away (ErrorCode_InvalidOperation on a
 *  number). */
Error UIPropertyBox_SetDefaultText(UIScreen* screen, uint64_t widgetId, bool hasDefault, const unsigned char* text);
/** @brief Reads whether the box is being typed in into @p outIsTyping (not NULL). */
Error UIPropertyBox_IsTyping(UIScreen* screen, uint64_t widgetId, bool* outIsTyping);


// Behaviour helpers, for looks and the kit.
/**
 * @brief Constructs the capability and the text box under it from the kind's arguments.
 * @param base The look base (the widget, the text box, the property box); must not be NULL.
 * @param args The arguments; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for value settings that break a rule, a value that is not finite, limits that are not
 *          valid or a unit too long; ErrorCode_InvalidTextEncoding for text that is not valid UTF-8; otherwise the options' error.
 *          Nothing is left to release on failure.
 */
Error UIPropertyBoxCapability_Construct(UIPropertyBoxLookBase* base, const UIPropertyBoxArgs* args);

/**
 * @brief Releases the capability and the text box under it.
 * @param base The look base; must not be NULL.
 * @returns Success, or the text box's release error.
 */
Error UIPropertyBoxCapability_Deconstruct(UIPropertyBoxLookBase* base);

/**
 * @brief Returns the state a look shows: disabled, pressed (a press or a scrub), invalid (typed text that is no number), entered (typing),
 *        hovered, focused or normal.
 * @param base The look base; must not be NULL.
 * @returns The state.
 */
UIElementState UIPropertyBoxCapability_GetState(const UIPropertyBoxLookBase* base);

/**
 * @brief Returns whether the box is being typed in (entered, or on through its own context menu): a look draws the text box's text, its
 *        selection and its caret then.
 * @param base The look base; must not be NULL.
 * @returns true while typing.
 */
bool UIPropertyBoxCapability_IsTyping(const UIPropertyBoxLookBase* base);

/**
 * @brief Returns what shows while the box is not typed in: the value at its precision, a dash while mixed, or the string.
 * @param self The capability; must not be NULL.
 * @returns The content (its revision moves with the text).
 */
static inline const UITextContent* UIPropertyBoxCapability_GetShownText(const UIPropertyBoxCapability* self)
{
    return &self->_shownText;
}

/**
 * @brief Returns the unit a number shows after it.
 * @param self The capability; must not be NULL.
 * @returns The content ("" for none).
 */
static inline const UITextContent* UIPropertyBoxCapability_GetUnit(const UIPropertyBoxCapability* self)
{
    return &self->_unit;
}

/**
 * @brief Returns whether the unit shows: a number that is not mixed, with a unit.
 * @param self The capability; must not be NULL.
 * @returns true when it does.
 */
bool UIPropertyBoxCapability_ShowsUnit(const UIPropertyBoxCapability* self);

/**
 * @brief Returns whether the box shows a fill bar: a number with a drag range that ShowsFill and is not mixed.
 * @param self The capability; must not be NULL.
 * @returns true when it does.
 */
bool UIPropertyBoxCapability_ShowsFill(const UIPropertyBoxCapability* self);

/**
 * @brief Returns how much of the box the fill bar covers: the value's share of the drag range, through the conversion.
 * @param self The capability; must not be NULL.
 * @returns The share in [0;1].
 */
double UIPropertyBoxCapability_GetFillShare(const UIPropertyBoxCapability* self);

/**
 * @brief Returns whether the box holds a string.
 * @param self The capability; must not be NULL.
 * @returns true for a string.
 */
static inline bool UIPropertyBoxCapability_IsString(const UIPropertyBoxCapability* self)
{
    return self->_isString;
}

/**
 * @brief Gives a box its context menu, what the kit does when it makes one: Reset, Copy and Paste, or the text box's menu while it is typed
 *        in, through the context menu service, so calling code may set a provider of its own in its place.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no property box; otherwise the service's error.
 */
Error UIPropertyBox_AttachContextMenu(UIScreen* screen, uint64_t widgetId);
