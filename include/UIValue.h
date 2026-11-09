#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"
#include "UIWidget.h"


/**
 * @file UIValue.h
 * @brief The value piece: a number, as a double, that a slider, a property box and a compound slider all hold and answer through the
 *        same functions (UIValue_Set(screen, id, 0.5)), so calling code does not care which element it has.
 *
 * WHAT IT HOLDS (UIValueSettings): hard limits, each with a flag of its own (HasMinimum, HasMaximum); a soft range inside them, which a
 * drag and the keys keep to while typing goes to the hard limits; whole numbers or not; a step or a list of allowed values (never
 * both); a conversion between a share of the track and the value; a default; how much Shift slows a drag (FineFactor). A capability
 * (UIValueCapability) adds the value itself, whether it is enabled and a mixed state (several objects differ).
 *
 * THE ORDER OF OPERATIONS, always: the pointer, then the share of the track (UIValue_GetPointerFraction), then the conversion, then the
 * step or the value list, then the limits (UIValueSettings_FromShare). A value set by code is only clamped and snapped
 * (UIValueSettings_Normalize): converting it for display (UIValueSettings_ToShare) never changes it, so a value never moves because it
 * was shown.
 *
 * CONVERSIONS map a share of the drag range in [0;1] to a value and back: linear; log (the hard minimum above 0); power with an
 * exponent; even over a value list (each allowed value gets the same length of track); a custom pair of functions, whose answers are
 * checked at every call (a NaN or a value out of the range is clamped, never stored).
 *
 * The arithmetic is pure and defined for every double: NaN, infinities and denormals give the documented answers, never a NaN.
 * The capability's change helpers (UIValueCapability_BeginChange and the rest) are what an element posts its value events with:
 * Changed while a value moves and may go on, Committed when the change is done (not when it ended where it began), Cancelled when the
 * value at its start comes back. Several values (mixed) made one is a change too, even to the number the piece held. Code that sets the
 * value or the mixed state during a change is where that change now starts: a cancel goes back to code's value, and a commit counts from
 * it; code that changes a setting keeps the start (inside the new settings).
 *
 * Calling code uses the UIValue_* functions by widget id: a stale id, or a widget that holds no value piece, is refused. Code setting a
 * value or a setting posts no event; a setter whose result would break a rule changes nothing.
 */


// Forward declarations.
/** @brief The screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;
/** @brief A context menu request; full type in UIContextMenu.h. */
typedef struct UIContextMenuRequestStruct UIContextMenuRequest;


// Macros.
/** @brief The most values a value list holds. */
#define UI_VALUE_MAX_LIST_COUNT ((size_t)4096)
/** @brief How much Shift slows a drag and the keys unless a value says otherwise. */
#define UI_VALUE_DEFAULT_FINE_FACTOR 0.1
/** @brief The largest exponent a power conversion takes (and its smallest is its reciprocal). */
#define UI_VALUE_MAX_EXPONENT 64.0
/** @brief The share of the track an arrow, a wheel notch or UIValue_Step moves a value with no step by. */
#define UI_VALUE_KEY_STEP_SHARE 0.01
/** @brief The share of the track a coarse drag (Ctrl) snaps to on a conversion that is not linear. */
#define UI_VALUE_COARSE_SHARE 0.1
/** @brief How many UI pixels a scrub over a value with no range moves it by one step (a stepped or whole-number value). */
#define UI_VALUE_SCRUB_PIXELS_PER_STEP 4.0
/** @brief How many UI pixels a scrub over a value with no range and no step moves it by its magnitude (UIValueSettings_GetScrubRate). */
#define UI_VALUE_SCRUB_PIXELS_PER_MAGNITUDE 100.0
/** @brief The longest text UIValue_ParseText reads as a number, in bytes: a longer one is no number. */
#define UI_VALUE_MAX_TEXT_BYTES ((size_t)128)
/** @brief The most digits after the point a value is shown with (UIValue_WriteFixedText). */
#define UI_VALUE_MAX_PRECISION 15


// Types.
/**
 * @brief How a share of the drag range maps to a value and back.
 */
typedef enum UIValueConversionEnum
{
    /** @brief Evenly: the value grows with the share. */
    UIValueConversion_Linear,
    /** @brief By ratios: each equal stretch of track multiplies the value by the same factor. Needs the hard minimum above 0. */
    UIValueConversion_Log,
    /** @brief The share raised to an exponent (above 1: fine near the lower end; below 1: fine near the upper end). */
    UIValueConversion_Power,
    /** @brief Each value of the value list gets the same length of track, however far apart they are. Needs a list of at least two. */
    UIValueConversion_Even,
    /** @brief A pair of functions calling code gives (UIValueCustomConversion). */
    UIValueConversion_Custom
} UIValueConversion;

/**
 * @brief A custom conversion's way from a share to a value.
 *
 * Its answer is checked at every call: a NaN counts as @p minimum and a value outside [@p minimum, @p maximum] is clamped, so it may run
 * backwards or jump without a value ever leaving the range. It must not change the UI (it runs inside hooks and the look's drawing).
 * @param share The share of the drag range, in [0;1].
 * @param minimum The drag range's lower end.
 * @param maximum Its upper end.
 * @param userData The UserData stored with the conversion.
 * @returns The value.
 */
typedef double (*UIValueToValue)(double share, double minimum, double maximum, const UserData* userData);

/**
 * @brief A custom conversion's way from a value to a share: what the thumb, a fill bar and a drag's start show.
 *
 * Its answer is checked at every call: a NaN counts as 0 and an answer outside [0;1] is clamped. It must not change the UI.
 * @param value The value, clamped into [minimum, maximum] first (a NaN is the minimum).
 * @param minimum The drag range's lower end.
 * @param maximum Its upper end.
 * @param userData The UserData stored with the conversion.
 * @returns The share.
 */
typedef double (*UIValueToShare)(double value, double minimum, double maximum, const UserData* userData);

/**
 * @brief A custom conversion: the two functions and what they are called with. A plain value.
 */
typedef struct UIValueCustomConversionStruct
{
    /** @brief From a share to a value; must not be NULL for UIValueConversion_Custom. */
    UIValueToValue ToValue;
    /** @brief From a value to a share; must not be NULL for UIValueConversion_Custom. */
    UIValueToShare ToShare;
    /** @brief What both are called with. */
    UserData Data;
} UIValueCustomConversion;

/**
 * @brief What a value may be and how it moves: the pure part of the value piece. A plain value; build one with UIValueSettings_Create or
 *        UIValueSettings_CreateRange and check it with UIValueSettings_IsValid.
 */
typedef struct UIValueSettingsStruct
{
    /** @brief Whether Minimum applies. */
    bool HasMinimum;
    /** @brief The hard lower limit, when HasMinimum; finite. */
    double Minimum;
    /** @brief Whether Maximum applies. */
    bool HasMaximum;
    /** @brief The hard upper limit, when HasMaximum; finite, above Minimum with a finite span. */
    double Maximum;
    /** @brief Whether a drag and the keys keep to SoftMinimum and SoftMaximum (typing still goes to the hard limits). Not with a list. */
    bool HasSoftRange;
    /** @brief The soft range's lower end, when HasSoftRange; finite, inside the hard limits, on a step (a whole number for a whole-number
     *  value) or on a hard limit. */
    double SoftMinimum;
    /** @brief Its upper end; above SoftMinimum with a finite span, inside the hard limits, on a step or on a hard limit. */
    double SoftMaximum;
    /** @brief Whether the value is a whole number: with no step, a step of 1. The limits must hold one. */
    bool IsInteger;
    /** @brief Whether the value keeps to whole steps from the lower limit (from 0 without one); the upper limit counts as a step. Not with
     *  a list. */
    bool HasStep;
    /** @brief The step, when HasStep: finite, a normal double and at least 2^-50 of the largest limit's size (so that steps can be told
     *  apart there), a whole number for a whole-number value. */
    double Step;
    /** @brief The allowed values, rising strictly, each finite, inside the hard limits and whole for a whole-number value; NULL when
     *  ValueCount is 0. Borrowed: a capability keeps a copy of its own. */
    const double* ValueList;
    /** @brief How many, at most UI_VALUE_MAX_LIST_COUNT; 0 for no list. */
    size_t ValueCount;
    /** @brief How a share of the drag range maps to a value. */
    UIValueConversion Conversion;
    /** @brief The exponent of UIValueConversion_Power: finite, within [1 / UI_VALUE_MAX_EXPONENT, UI_VALUE_MAX_EXPONENT]. */
    double Exponent;
    /** @brief The functions of UIValueConversion_Custom. */
    UIValueCustomConversion Custom;
    /** @brief Whether it has a value to go back to (the reset action, a middle click). */
    bool HasDefault;
    /** @brief The value to go back to, when HasDefault; finite (clamped and snapped when it is used). */
    double Default;
    /** @brief How much Shift slows a drag and the keys' fine steps: within (0, 1]. */
    double FineFactor;
    /** @brief Whether the value stands for a float (a float row's): its text is a float's shortest that reads back, and a typed or pasted
     *  number is read as a float. A value set by code is kept as it is. */
    bool IsSinglePrecision;
} UIValueSettings;

/**
 * @brief What an element holding a value piece is created with. A plain value; build one with UIValueOptions_Create or
 *        UIValueOptions_CreateRange.
 */
typedef struct UIValueOptionsStruct
{
    /** @brief What the value may be and how it moves. Its list is copied. */
    UIValueSettings Settings;
    /** @brief The value; finite, clamped and snapped. */
    double Value;
    /** @brief Whether it takes input. */
    bool IsEnabled;
    /** @brief Whether it shows as mixed (several objects differ) until edited. */
    bool IsMixed;
} UIValueOptions;

/**
 * @brief How an element holding a value piece hears what code did to it, so it can end or adjust what it was doing. Static storage;
 *        every slot is optional.
 */
typedef struct UIValueHooksStruct
{
    /** @brief The value or a setting changed by code (UIValue_*); a change in progress goes on from it. */
    Error (*OnChangedByCode)(Widget* widget);
    /** @brief Its enabled state changed by code: it ends what it was doing and becomes focusable or not. */
    Error (*OnEnabledChanged)(Widget* widget);
    /** @brief A part whose value events this element takes (UIValueCapability_SetEventTarget) moved, by input or by code: a compound keeps
     *  its parts in step. */
    Error (*OnPartChanged)(Widget* widget, uint64_t partId);
    /** @brief The unit a pasted number may end with (" m"), UTF-8, borrowed until the next change of the element; NULL (the hook or its
     *  answer) for none. The value's context menu pastes with it, as the element's own paste does. */
    const unsigned char* (*GetUnit)(Widget* widget);
} UIValueHooks;

/**
 * @brief The value piece as a capability: the value, its settings, enabled, mixed and a change in progress. Embedded by an element's
 *        capability; its fields are read-only outside the value's module.
 */
typedef struct UIValueCapabilityStruct
{
    /** @brief The value. */
    double _value;
    /** @brief The settings; its list points at _list. */
    UIValueSettings _settings;
    /** @brief The copy of the value list, owned; NULL without one. */
    double* _list;
    /** @brief Whether it takes input. */
    bool _isEnabled;
    /** @brief Whether it shows as mixed. */
    bool _isMixed;
    /** @brief Whether its element needs both ends of a drag range (a slider's track). */
    bool _requiresRange;
    /** @brief Whether a change is in progress (BeginChange to EndChange or CancelChange). */
    bool _isChanging;
    /** @brief The value and the mixed state when the change in progress began. */
    double _changeStartValue;
    bool _changeStartIsMixed;
    /** @brief What the element hears from code; static storage, may be NULL. */
    const UIValueHooks* _hooks;
    /** @brief The widget whose id its value events are posted under (a compound's, a row's); WIDGET_ID_INVALID for its own. */
    uint64_t _eventTargetId;
    /** @brief Whether its events carry an index, and the index (a row's axis). */
    bool _hasEventIndex;
    int32_t _eventIndex;
    /** @brief Moves with every change of the settings. */
    uint64_t _settingsRevision;
} UIValueCapability;


// Helpers.
/**
 * @brief Settings with no limits: no step, no list, linear, no default, the default fine factor.
 * @returns The settings.
 */
UIValueSettings UIValueSettings_Create(void);

/**
 * @brief Settings with both hard limits; the rest as UIValueSettings_Create.
 * @param minimum The lower limit.
 * @param maximum The upper limit.
 * @returns The settings (UIValueSettings_IsValid tells whether the range is one).
 */
UIValueSettings UIValueSettings_CreateRange(double minimum, double maximum);

/**
 * @brief Options with the range [0, 1] at 0, enabled, not mixed.
 * @returns The options.
 */
UIValueOptions UIValueOptions_Create(void);

/**
 * @brief Options with a range and a value; the rest as UIValueOptions_Create.
 * @param minimum The lower limit.
 * @param maximum The upper limit.
 * @param value The value.
 * @returns The options.
 */
UIValueOptions UIValueOptions_CreateRange(double minimum, double maximum, double value);


// The value's arithmetic (pure).
/**
 * @brief Checks every rule of a set of settings (each field's own, and the ones between them).
 * @param settings The settings; must not be NULL.
 * @param requiresRange Whether both ends of a drag range are needed (a slider's track).
 * @param outReason [out] Receives what is wrong, a static string; may be NULL.
 * @returns true when every rule holds.
 */
bool UIValueSettings_IsValid(const UIValueSettings* settings, bool requiresRange, const char** outReason);

/**
 * @brief Returns the range a drag and the keys work in, as the settings give it: the soft range, else both hard limits, else a value
 *        list's ends.
 * @param settings The settings; must not be NULL and valid.
 * @param outMinimum [out] Receives the lower end; must not be NULL.
 * @param outMaximum [out] Receives the upper end; must not be NULL.
 * @returns false when there is none (both left alone).
 */
bool UIValueSettings_TryGetDragRange(const UIValueSettings* settings, double* outMinimum, double* outMaximum);

/**
 * @brief Returns the drag range around a value: the soft range widened to take in a value outside it (typed past it), within the hard
 *        limits, so a drag or a key from there starts where the value is instead of jumping into the soft range.
 * @param settings The settings; must not be NULL and valid.
 * @param value The value; one that is not finite widens nothing.
 * @param outMinimum [out] Receives the lower end; must not be NULL.
 * @param outMaximum [out] Receives the upper end; must not be NULL.
 * @returns false when there is no drag range (both left alone).
 */
bool UIValueSettings_GetDragRangeAround(const UIValueSettings* settings, double value, double* outMinimum, double* outMaximum);

/**
 * @brief Clamps a value into the hard limits and snaps it onto the step, the value list or whole numbers: what a value set by code, typed
 *        or pasted becomes. Clamping comes first, and the upper limit counts as a step, so it stays reachable.
 * @param settings The settings; must not be NULL and valid.
 * @param value The value; a NaN counts as the lower limit (0 without one), an infinity as the limit on its side (the largest finite
 *        double without one).
 * @returns The value, always finite.
 */
double UIValueSettings_Normalize(const UIValueSettings* settings, double value);

/**
 * @brief Converts a share of a drag range to a value: through the conversion, onto the step, the list or whole numbers, then into the
 *        range (whose ends count as steps).
 * @param settings The settings; must not be NULL and valid.
 * @param minimum The drag range's lower end (UIValueSettings_TryGetDragRange or UIValueSettings_GetDragRangeAround).
 * @param maximum Its upper end; above @p minimum.
 * @param share The share; a NaN counts as 0, outside [0;1] it is clamped.
 * @returns The value, always finite and inside [@p minimum, @p maximum].
 */
double UIValueSettings_FromShare(const UIValueSettings* settings, double minimum, double maximum, double share);

/**
 * @brief Converts a value to its share of a drag range, through the conversion: where the thumb or a fill bar shows it. Never changes the
 *        value: it only reads it.
 * @param settings The settings; must not be NULL and valid.
 * @param minimum The drag range's lower end.
 * @param maximum Its upper end; above @p minimum.
 * @param value The value; a NaN counts as the lower end.
 * @returns The share in [0;1].
 */
double UIValueSettings_ToShare(const UIValueSettings* settings, double minimum, double maximum, double value);

/**
 * @brief Moves a value by steps: what the arrows, the wheel and UIValue_Step do.
 *
 * With a list it goes from list value to list value; with a step (or whole numbers) by whole steps from the one it is on (from between
 * two, the next one that way), @p count rounded to whole steps (less than half a step moves nothing), and a fine step is still one step;
 * otherwise by UI_VALUE_KEY_STEP_SHARE of the drag range through the conversion (times FineFactor when fine), or with no drag range by the
 * value's magnitude (UIValueSettings_GetScrubRate times UI_VALUE_SCRUB_PIXELS_PER_MAGNITUDE times UI_VALUE_KEY_STEP_SHARE). It stays in
 * the drag range around the value (UIValueSettings_GetDragRangeAround), else in the hard limits.
 * @param settings The settings; must not be NULL and valid.
 * @param value The value; a NaN counts as the lower limit.
 * @param count How many steps, up for positive; a NaN is none, an infinity goes to the limit on its side.
 * @param isFine Whether Shift slows it.
 * @returns The value, always finite.
 */
double UIValueSettings_Step(const UIValueSettings* settings, double value, double count, bool isFine);

/**
 * @brief Returns how far a scrub over a value with no drag range moves it per UI pixel: a step (or 1 for whole numbers) every
 *        UI_VALUE_SCRUB_PIXELS_PER_STEP pixels; with no step, the value's magnitude (the power of ten at or below its size, at least 1) every
 *        UI_VALUE_SCRUB_PIXELS_PER_MAGNITUDE pixels.
 * @param settings The settings; must not be NULL and valid.
 * @param value The value the scrub started from; one that is not finite counts as 0.
 * @returns The rate, finite and above 0.
 */
double UIValueSettings_GetScrubRate(const UIValueSettings* settings, double value);

/**
 * @brief Snaps a value coarsely, what Ctrl does to a drag: on a linear drag range, or with no range, to whole multiples of a power of ten
 *        (a tenth of the range's own power of ten, or a tenth of the value's magnitude), never finer than the step; on another conversion
 *        the share snaps to whole UI_VALUE_COARSE_SHARE instead; a list is left alone. Then it is snapped and clamped as a drag's value.
 * @param settings The settings; must not be NULL and valid.
 * @param minimum The drag range's lower end, when @p hasRange.
 * @param maximum Its upper end, when @p hasRange.
 * @param hasRange Whether there is a drag range.
 * @param value The value; a NaN counts as the lower end.
 * @param magnitudeValue The value whose magnitude sets the increment with no range (the scrub's start).
 * @returns The value, always finite.
 */
double UIValueSettings_SnapCoarse(const UIValueSettings* settings, double minimum, double maximum, bool hasRange, double value,
    double magnitudeValue);

/**
 * @brief Clamps a value into a range and, with a step, onto the nearest whole step from the lower end. The upper end counts as a step,
 *        so it stays reachable when the range is not a whole number of steps.
 * @param value The value; a NaN gives the lower end.
 * @param minimum The lower end.
 * @param maximum The upper end.
 * @param hasStep Whether it keeps to steps.
 * @param step The step, when @p hasStep; one that is no positive finite number, or too small to count with, is no step.
 * @returns The value.
 */
double UIValue_ClampAndSnapToStep(double value, double minimum, double maximum, bool hasStep, double step);

/**
 * @brief Moves a value by steps in a range.
 *
 * With a step it goes to whole steps from the one it is on (a value between two counts from the next one in that direction), so from an
 * upper end that is no whole step it comes to the last one below it. @p count is rounded to whole steps there: less than half a step
 * moves nothing. Without a step a step is a hundredth of the range. The answer stays in the range.
 * @param value The value; a NaN counts as the lower end.
 * @param minimum The lower end.
 * @param maximum The upper end.
 * @param hasStep Whether it keeps to steps.
 * @param step The step, when @p hasStep; one that is no positive finite number, or too small to count with, is no step.
 * @param count How many steps, up for positive; a NaN is none.
 * @returns The value.
 */
double UIValue_GetSteppedFrom(double value, double minimum, double maximum, bool hasStep, double step, double count);

/**
 * @brief Returns where a value lies in a range, linearly.
 * @param value The value.
 * @param minimum The lower end.
 * @param maximum The upper end.
 * @returns The share in [0;1]; 0 for an empty range.
 */
double UIValue_GetFraction(double value, double minimum, double maximum);

/**
 * @brief Returns the share of a track a pointer means: a thumb's centre follows the pointer, so its centre travels the box less the
 *        thumb's length, and past the ends the share stays at 0 or 1.
 * @param pointer The pointer along the box, from its start.
 * @param boxLength The box's length.
 * @param thumbLength The thumb's length, in the same unit.
 * @returns The share in [0;1]; 0 when the box is no longer than the thumb.
 */
double UIValue_GetPointerFraction(double pointer, double boxLength, double thumbLength);

/**
 * @brief Returns the share of a track a pointer means, as UIValue_GetPointerFraction but not clamped: past an end it goes on below 0 or
 *        above 1, which a drag needs to tell when the pointer has passed the end.
 * @param pointer The pointer along the box, from its start.
 * @param boxLength The box's length.
 * @param thumbLength The thumb's length, in the same unit.
 * @returns The share; 0 when the box is no longer than the thumb or a length is not a finite number.
 */
double UIValue_GetUnclampedPointerFraction(double pointer, double boxLength, double thumbLength);


// Text (pure).
/**
 * @brief Writes the shortest text that reads back to exactly a value: what a value shows while it is typed in and what copy puts on the
 *        clipboard, so showing a value never changes it, far doubles included. A float's shortest when @p isSinglePrecision and the value
 *        is exactly a float.
 * @param value The value; finite (a NaN or an infinity writes as its token, which UIValue_ParseText refuses).
 * @param isSinglePrecision Whether the value stands for a float.
 * @param outText [out] The text is appended to it (a byte buffer), with a NUL past its count; must not be NULL.
 * @returns Success; the formatter's error (ErrorCode_BufferTooSmall for a buffer that cannot grow).
 */
Error UIValue_WriteShortestText(double value, bool isSinglePrecision, GenericBuffer* outText);

/**
 * @brief Writes a value with a number of digits after the point (none for a whole-number value's text): what a value shows while it is not
 *        typed in. Showing only reads the value. A negative zero shows as zero.
 * @param value The value.
 * @param precision Digits after the point, within [0, UI_VALUE_MAX_PRECISION] (clamped).
 * @param outText [out] The text is appended to it (a byte buffer), with a NUL past its count; must not be NULL.
 * @returns Success; the formatter's error.
 */
Error UIValue_WriteFixedText(double value, int32_t precision, GenericBuffer* outText);

/**
 * @brief Reads a number from text that came from a user or the clipboard (hostile): spaces around it and a unit after it are passed over,
 *        the point is a period, an exponent is allowed; a NaN, an infinity, anything else around it or text longer than
 *        UI_VALUE_MAX_TEXT_BYTES is no number.
 * @param text The text, UTF-8 or not; may be NULL only when @p length is 0.
 * @param length Its length in bytes (a NUL inside ends it).
 * @param unit A unit that may follow the number (" m"), or NULL for none.
 * @param isSinglePrecision Whether the number is read as a float (the nearest float, widened).
 * @param outValue [out] Receives the number when it is one; must not be NULL.
 * @returns true when the text is a finite number.
 */
bool UIValue_ParseText(const unsigned char* text, size_t length, const unsigned char* unit, bool isSinglePrecision, double* outValue);


// Behaviour helpers, for the elements that hold the piece.
/**
 * @brief Constructs the piece from its options.
 * @param self The piece; must not be NULL.
 * @param options The options; must not be NULL. Its list is copied.
 * @param hooks What the element hears from code; static storage, may be NULL.
 * @param requiresRange Whether the element needs both ends of a drag range (a slider).
 * @returns Success; ErrorCode_ArgumentOutOfRange for settings that break a rule or a value that is not finite. Nothing is left to
 *          release on failure.
 */
Error UIValueCapability_Construct(UIValueCapability* self, const UIValueOptions* options, const UIValueHooks* hooks, bool requiresRange);

/**
 * @brief Releases the piece's list.
 * @param self The piece; must not be NULL.
 */
void UIValueCapability_Deconstruct(UIValueCapability* self);

/**
 * @brief Replaces the piece's settings after checking them, keeping the value clamped and snapped (and a change's start). Hears nothing:
 *        the by-id setters call the element's hook after it.
 * @param self The piece; must not be NULL.
 * @param settings The settings; must not be NULL. Its list is copied.
 * @returns Success; ErrorCode_ArgumentOutOfRange for settings that break a rule, which changes nothing.
 */
Error UIValueCapability_SetSettings(UIValueCapability* self, const UIValueSettings* settings);

/**
 * @brief Makes the piece post its value events under another widget's id (a compound's, a row's), with an index or not, and tell that
 *        widget's hook (UIValueHooks.OnPartChanged) whenever it moves.
 * @param self The piece; must not be NULL.
 * @param targetId The widget; WIDGET_ID_INVALID for its own.
 * @param hasIndex Whether its events carry an index.
 * @param index The index, when @p hasIndex.
 */
void UIValueCapability_SetEventTarget(UIValueCapability* self, uint64_t targetId, bool hasIndex, int32_t index);

/**
 * @brief Starts a change (a drag, a held key, a scrub): what Committed and Cancelled count from.
 * @param self The piece; must not be NULL.
 */
void UIValueCapability_BeginChange(UIValueCapability* self);

/**
 * @brief During a change, makes what the piece holds now (its value and mixed state) where the change counts from: what code changed
 *        becomes no part of the change (the UIValue_* setters of the value and the mixed state do it; a compound does it for the parts it
 *        sets so). Nothing without a change.
 * @param self The piece; must not be NULL.
 */
void UIValueCapability_RebaseChange(UIValueCapability* self);

/**
 * @brief Moves the value during a change, by input, to a value already in its final form (from UIValueSettings_FromShare, _Step,
 *        _SnapCoarse or _Normalize): Changed when it moved or was mixed (the mixed state ends).
 * @param self The piece; must not be NULL.
 * @param widget Its element's widget; must not be NULL.
 * @param value The value; one that is not finite changes nothing.
 * @param outHasMoved [out] Receives whether it moved; may be NULL.
 * @returns Success, or the event queue's or a hook's error.
 */
Error UIValueCapability_MoveTo(UIValueCapability* self, Widget* widget, double value, bool* outHasMoved);

/**
 * @brief Ends the change in progress: Committed unless the value and the mixed state ended as they began. Nothing without one.
 * @param self The piece; must not be NULL.
 * @param widget Its element's widget; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIValueCapability_EndChange(UIValueCapability* self, Widget* widget);

/**
 * @brief Takes the change in progress back: the value and the mixed state at its start come back, and Cancelled is posted (when
 *        either had changed). Nothing without one.
 * @param self The piece; must not be NULL.
 * @param widget Its element's widget; must not be NULL.
 * @returns Success, or the event queue's or a hook's error.
 */
Error UIValueCapability_CancelChange(UIValueCapability* self, Widget* widget);

/**
 * @brief A change that is whole at once (a wheel notch, a reset, a paste, a commit of typing) to a value already in its final form (from
 *        UIValueSettings_Normalize or _Step): Changed then Committed and the ValueStep cue when it moved or was mixed (the mixed state
 *        ends). Refused during another change (nothing happens).
 * @param self The piece; must not be NULL.
 * @param widget Its element's widget; must not be NULL.
 * @param value The value; one that is not finite changes nothing.
 * @param outHasMoved [out] Receives whether it moved; may be NULL.
 * @returns Success, or the event queue's or a hook's error.
 */
Error UIValueCapability_ApplyWholeChange(UIValueCapability* self, Widget* widget, double value, bool* outHasMoved);

/**
 * @brief Returns the value's share of the drag range, for a look: 0 without one.
 * @param self The piece; must not be NULL.
 * @returns The share in [0;1].
 */
double UIValueCapability_GetShare(const UIValueCapability* self);

/**
 * @brief Returns whether the piece has a drag range (a fill bar, a range to scrub across).
 * @param self The piece; must not be NULL.
 * @returns true when it has one.
 */
bool UIValueCapability_HasDragRange(const UIValueCapability* self);

/**
 * @brief Returns the value.
 * @param self The piece; must not be NULL.
 * @returns The value.
 */
static inline double UIValueCapability_GetValue(const UIValueCapability* self)
{
    return self->_value;
}

/**
 * @brief Returns whether a change is in progress.
 * @param self The piece; must not be NULL.
 * @returns true between BeginChange and its end.
 */
static inline bool UIValueCapability_IsChanging(const UIValueCapability* self)
{
    return self->_isChanging;
}

/**
 * @brief Returns whether the piece takes input.
 * @param self The piece; must not be NULL.
 * @returns true when it does.
 */
static inline bool UIValueCapability_IsEnabled(const UIValueCapability* self)
{
    return self->_isEnabled;
}

/**
 * @brief Returns whether the piece shows as mixed.
 * @param self The piece; must not be NULL.
 * @returns true when it does.
 */
static inline bool UIValueCapability_IsMixed(const UIValueCapability* self)
{
    return self->_isMixed;
}

/**
 * @brief Returns the piece's settings (its list borrowed: valid until the settings change).
 * @param self The piece; must not be NULL.
 * @returns The settings.
 */
static inline const UIValueSettings* UIValueCapability_GetSettings(const UIValueCapability* self)
{
    return &self->_settings;
}

/**
 * @brief Sets the value by code, clamped and snapped, without hearing hooks or posting: what a compound uses to keep its parts in step.
 * @param self The piece; must not be NULL.
 * @param value The value; one that is not finite changes nothing.
 */
void UIValueCapability_SetValueQuietly(UIValueCapability* self, double value);

/**
 * @brief Sets the enabled and mixed states by code without hearing hooks: what a compound uses to keep its parts in step.
 * @param self The piece; must not be NULL.
 * @param isEnabled Whether it takes input.
 * @param isMixed Whether it shows as mixed.
 */
void UIValueCapability_SetStatesQuietly(UIValueCapability* self, bool isEnabled, bool isMixed);

/**
 * @brief Puts the value on the clipboard as its shortest text (UIValue_WriteShortestText), without a unit: what copy does on a value
 *        element, focused or under the pointer.
 * @param self The piece; must not be NULL.
 * @param widget Its element's widget; must not be NULL.
 * @returns Success (also with no input to copy through); the clipboard's error.
 */
Error UIValueCapability_Copy(const UIValueCapability* self, Widget* widget);

/**
 * @brief Reads a number from the clipboard (UIValue_ParseText, at most UI_VALUE_MAX_TEXT_BYTES of it) and makes it the value as a whole
 *        change, clamped into the hard limits and snapped; text that is no number posts the Refused cue and changes nothing.
 * @param self The piece; must not be NULL.
 * @param widget Its element's widget; must not be NULL.
 * @param unit A unit the text may end with, or NULL.
 * @param outIsPasted [out] Receives whether a number was read; may be NULL.
 * @returns Success; the clipboard's, the event queue's or a hook's error.
 */
Error UIValueCapability_Paste(UIValueCapability* self, Widget* widget, const unsigned char* unit, bool* outIsPasted);

/**
 * @brief Gives a value element its context menu: Reset (with a default), Copy and Paste, through the context menu service, so calling code
 *        may set a provider of its own in its place. The kit gives one to every slider.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that holds no value piece; otherwise the service's error.
 */
Error UIValue_AttachContextMenu(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Opens a value element's context menu for a request: what its provider does, and what an element with a menu of its own (a property
 *        box while it is not typed in) calls. Nothing opens during a change (a right click during a drag), which the menu would end.
 * @param screen The screen; must not be NULL.
 * @param request The request; must not be NULL.
 * @param widgetId The element whose value the menu acts on, found again by id when an item is chosen.
 * @returns Success (nothing opens for a widget that holds no value piece); the menu's error.
 */
Error UIValue_OpenContextMenu(UIScreen* screen, const UIContextMenuRequest* request, uint64_t widgetId);

/**
 * @brief Finds the value piece of a widget by id: what every UIValue_* function and the elements that hold several values use.
 * @param screen The widget's screen; must not be NULL.
 * @param widgetId The widget.
 * @param outWidget [out] Receives the widget; must not be NULL.
 * @param outValue [out] Receives its piece; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a screen with no kit or a widget that holds no value piece.
 */
Error UIValue_Resolve(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UIValueCapability** outValue);


// Calling code: by widget id.
/*
 * Each takes the element's screen (not NULL) and its widget id, and returns ErrorCode_InvalidOperation for a stale id or a widget that
 * holds no value piece. A setter whose result breaks a rule returns ErrorCode_ArgumentOutOfRange and changes nothing. None posts an
 * event; the element hears what changed (a change in progress goes on from the new value).
 */
/** @brief Sets the value, clamped and snapped; a value that is not finite is refused (ErrorCode_ArgumentOutOfRange). */
Error UIValue_Set(UIScreen* screen, uint64_t widgetId, double value);
/** @brief Reads the value into @p outValue (not NULL). */
Error UIValue_Get(UIScreen* screen, uint64_t widgetId, double* outValue);
/** @brief Sets the hard limits, each with its flag; the value is clamped into them. */
Error UIValue_SetRange(UIScreen* screen, uint64_t widgetId, bool hasMinimum, double minimum, bool hasMaximum, double maximum);
/** @brief Sets the soft range a drag and the keys keep to, or takes it away. */
Error UIValue_SetSoftRange(UIScreen* screen, uint64_t widgetId, bool hasSoftRange, double minimum, double maximum);
/** @brief Sets whether the value keeps to whole steps, and the step; the value is snapped. */
Error UIValue_SetStep(UIScreen* screen, uint64_t widgetId, bool hasStep, double step);
/** @brief Sets whether the value is a whole number; the value is snapped. */
Error UIValue_SetIsInteger(UIScreen* screen, uint64_t widgetId, bool isInteger);
/** @brief Sets the allowed values (copied; @p values may be NULL only with a count of 0, which takes the list away); the value snaps to
 *  the nearest. Taking the list away leaves an even conversion linear. */
Error UIValue_SetValueList(UIScreen* screen, uint64_t widgetId, const double* values, size_t count);
/** @brief Sets the conversion and a power conversion's exponent (read only for UIValueConversion_Power); UIValueConversion_Custom goes
 *  through UIValue_SetCustomConversion. */
Error UIValue_SetConversion(UIScreen* screen, uint64_t widgetId, UIValueConversion conversion, double exponent);
/** @brief Sets a custom conversion: both functions (not NULL) and what they are called with (@p userData may be NULL for none; copied). */
Error UIValue_SetCustomConversion(UIScreen* screen, uint64_t widgetId, UIValueToValue toValue, UIValueToShare toShare,
    const UserData* userData);
/** @brief Sets the value the reset action and a middle click go back to, or takes it away. */
Error UIValue_SetDefault(UIScreen* screen, uint64_t widgetId, bool hasDefault, double value);
/** @brief Puts the value back to its default, clamped and snapped (ErrorCode_InvalidOperation with no default). */
Error UIValue_ResetToDefault(UIScreen* screen, uint64_t widgetId);
/** @brief Sets how much Shift slows a drag and the keys' fine steps, within (0, 1]. */
Error UIValue_SetFineFactor(UIScreen* screen, uint64_t widgetId, double fineFactor);
/** @brief Sets whether it shows as mixed (several objects differ): until edited, or until code says otherwise. */
Error UIValue_SetMixed(UIScreen* screen, uint64_t widgetId, bool isMixed);
/** @brief Reads whether it shows as mixed into @p outIsMixed (not NULL). */
Error UIValue_IsMixed(UIScreen* screen, uint64_t widgetId, bool* outIsMixed);
/** @brief Moves the value by steps as the keys would (UIValueSettings_Step), by code. */
Error UIValue_Step(UIScreen* screen, uint64_t widgetId, double count, bool isFine);
/** @brief Enables or disables it: disabled, it ends a change in progress (taken back), takes no focus and swallows presses. */
Error UIValue_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);
/** @brief Reads the settings into @p outSettings (not NULL); its list points into the element, valid until the settings change. */
Error UIValue_GetSettings(UIScreen* screen, uint64_t widgetId, UIValueSettings* outSettings);
