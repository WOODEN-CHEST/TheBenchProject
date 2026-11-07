#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "GameKey.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UISlider.h
 * @brief The slider element: a value in a range, by a drag, the arrows or the wheel, with an optional step and an optional default.
 *
 * It is an edit element (UIWidget.h, "EDIT ELEMENTS"): moving the focus onto it changes nothing, and its keys reach it only once
 * it is entered, by the accept action (Enter), any press on it or code; the back action (Escape) or the accept action leaves it,
 * and Tab leaves it and goes on. A press on the track puts the thumb under the pointer; a press on the thumb holds it where it was
 * grabbed, so a click on it changes nothing. Dragging then maps the pointer to the value, clamped when the pointer leaves the
 * box, the screen or the window: past an end the value is exactly at the limit. Entered, the arrows step (ui_up and ui_right
 * higher, ui_down and ui_left lower, repeats following; at an end they do nothing and are still its), Home and End jump. The wheel
 * changes it only while it is entered or with Ctrl: otherwise it goes on up the tree, so scrolling a panel never changes a value
 * by accident. The reset action puts it back to its default, focused or only under the pointer, entered or not. A change posts
 * UIElementEvent_Changed each time the value moves, then UIElementEvent_Committed at the release or the key's release (each
 * arrow press is a whole change; a wheel notch and a reset are both at once) unless the value ended where it began; Escape during
 * one puts the first value back and posts UIElementEvent_Cancelled, and the slider stays entered. With no change going on it leaves
 * Escape to the screen, which leaves it: the presses already committed stay. Leaving it any way ends a change in progress (a drag,
 * a held key) with its commit, and what is left of that drag or key does nothing; a cancel by code takes the change back. A press
 * that stops reaching it ends the change at once, with its commit. The events carry the value as a double. Code setting the value posts nothing, and a value that is not a
 * finite number is refused. It also posts the events every element shares (UIElement_PostMouseEvents).
 *
 * Calling code uses the UISlider_* functions by widget id; widget types of their own embed the capability, call the
 * UISliderCapability_* helpers and are edit elements too (their descriptor's NeedsEntering). The value's arithmetic is pure, at
 * the end of this file.
 */


// Types.
/**
 * @brief What a slider is created with (UIKit_CreateSlider). A plain value; build one with UISliderOptions_Create.
 */
typedef struct UISliderOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief The range's lower end; finite and below Maximum, with a finite span. */
    double Minimum;
    /** @brief The range's upper end. */
    double Maximum;
    /** @brief The value; clamped into the range and onto the step. */
    double Value;
    /** @brief Whether the value keeps to whole steps from Minimum. */
    bool HasStep;
    /** @brief The step, when HasStep; finite and > 0. */
    double Step;
    /** @brief Whether it takes input. */
    bool IsEnabled;
    /** @brief Whether it has a value to go back to (the reset action). */
    bool HasDefault;
    /** @brief The value to go back to, when HasDefault; finite. Clamped into the range and onto the step when it is used. */
    double Default;
} UISliderOptions;

/**
 * @brief What a slider type's constructor gets (UIKit_CreateSlider passes it): the kit and the options.
 */
typedef struct UISliderArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UISliderOptions* Options;
} UISliderArgs;

/**
 * @brief A slider's capability. Embedded by a widget type; its fields are read-only outside the slider's module.
 */
typedef struct UISliderCapabilityStruct
{
    /** @brief The look. */
    UIElementLook _look;
    /** @brief The value. */
    double _value;
    /** @brief The range's lower end. */
    double _minimum;
    /** @brief The range's upper end. */
    double _maximum;
    /** @brief Whether the value keeps to whole steps. */
    bool _hasStep;
    /** @brief The step. */
    double _step;
    /** @brief Whether it takes input. */
    bool _isEnabled;
    /** @brief Whether it has a value to go back to. */
    bool _hasDefault;
    /** @brief The value to go back to. */
    double _default;
    /** @brief A left press on it is being dragged. */
    bool _isDragging;
    /** @brief The key whose hold is a change in progress, or GameKey_Unknown. */
    GameKey _sessionKey;
    /** @brief The key whose change was cancelled, or ended by leaving the slider: its repeats and release do nothing until it is pressed
     *  anew. GameKey_Unknown when none. */
    GameKey _endedKey;
    /** @brief The value when the change in progress began. */
    double _sessionStartValue;
    /** @brief How far from the thumb's centre a drag holds it, in surface pixels. */
    double _grabOffsetPixels;
} UISliderCapability;


// Helpers.
/**
 * @brief Default options: the range [0, 1] at 0, no step, enabled, no default.
 * @returns The options.
 */
static inline UISliderOptions UISliderOptions_Create(void)
{
    return (UISliderOptions)
    {
        .Element = UIElementOptions_Create(),
        .Minimum = 0.0,
        .Maximum = 1.0,
        .Value = 0.0,
        .HasStep = false,
        .Step = 0.0,
        .IsEnabled = true,
        .HasDefault = false,
        .Default = 0.0
    };
}


// Calling code: by widget id.
/**
 * @brief Sets a slider's value, clamped into its range and onto its step. Posts no event. A change in progress goes on from it.
 * @param screen The slider's screen; must not be NULL.
 * @param widgetId The slider.
 * @param value The value; finite.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no slider; ErrorCode_ArgumentOutOfRange for
 *          a NaN or an infinite value, which changes nothing.
 */
Error UISlider_SetValue(UIScreen* screen, uint64_t widgetId, double value);

/**
 * @brief Reads a slider's value.
 * @param screen The slider's screen; must not be NULL.
 * @param widgetId The slider.
 * @param outValue [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no slider.
 */
Error UISlider_GetValue(UIScreen* screen, uint64_t widgetId, double* outValue);

/**
 * @brief Sets a slider's range; its value is clamped into it.
 * @param screen The slider's screen; must not be NULL.
 * @param widgetId The slider.
 * @param minimum The lower end.
 * @param maximum The upper end; above @p minimum, with a finite span.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no slider; ErrorCode_ArgumentOutOfRange for a
 *          range that is not valid, which changes nothing.
 */
Error UISlider_SetRange(UIScreen* screen, uint64_t widgetId, double minimum, double maximum);

/**
 * @brief Sets whether a slider's value keeps to whole steps from its lower end, and the step; its value is snapped.
 * @param screen The slider's screen; must not be NULL.
 * @param widgetId The slider.
 * @param hasStep Whether it does.
 * @param step The step, when @p hasStep; finite and > 0.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no slider; ErrorCode_ArgumentOutOfRange for a
 *          step that is not valid, which changes nothing.
 */
Error UISlider_SetStep(UIScreen* screen, uint64_t widgetId, bool hasStep, double step);

/**
 * @brief Enables or disables a slider. Disabling it cancels a change in progress and takes its focus away.
 * @param screen The slider's screen; must not be NULL.
 * @param widgetId The slider.
 * @param isEnabled Whether it takes input.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no slider; otherwise a hook's or the event
 *          queue's error.
 */
Error UISlider_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);

/**
 * @brief Sets the value a slider goes back to on the reset action.
 * @param screen The slider's screen; must not be NULL.
 * @param widgetId The slider.
 * @param hasDefault Whether it has one; without one the reset action goes on to what lies behind it.
 * @param value The value, when @p hasDefault; finite.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no slider; ErrorCode_ArgumentOutOfRange for a
 *          value that is not finite, which changes nothing.
 */
Error UISlider_SetDefault(UIScreen* screen, uint64_t widgetId, bool hasDefault, double value);


// Behaviour helpers, for widget types embedding the capability.
/**
 * @brief Constructs the capability from a widget type's constructor.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The constructor's arguments; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a range, a value or a step that is not valid.
 */
Error UISliderCapability_Construct(UISliderCapability* self, Widget* widget, const UISliderArgs* args);

/**
 * @brief Advances the look, and ends a key's change whose release cannot reach the slider any more; call it from Update.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param time The tick's time.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_Update(UISliderCapability* self, Widget* widget, ProgramTime time);

/**
 * @brief Interprets mouse input; call it from OnMouseInput.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnMouseInput(UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args);

/**
 * @brief Ends a drag whose press the screen says is lost: the value stays where it got to, and the change is committed; call it from
 *        OnPressLost.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param button The press's mouse button.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnPressLost(UISliderCapability* self, Widget* widget, GameKey button);

/**
 * @brief Interprets the wheel, which it takes only while entered or with Ctrl; call it from OnScrollInput.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when it takes the wheel. Must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnScrollInput(UISliderCapability* self, Widget* widget, const WidgetScrollInputArgs* args, bool* outIsHandled);

/**
 * @brief Ends the change in progress when the slider is left; call it from OnLeave.
 *
 * A drag or a held key's change ends there: committed on UILeaveReason_Keep (unless the value ended where it began), taken back on
 * UILeaveReason_Cancel. What is left of it does nothing: the drag's moves and release, the key's repeats and release (until it is
 * pressed anew). Nothing happens with no change in progress.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param reason Why the slider was left.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnLeave(UISliderCapability* self, Widget* widget, UILeaveReason reason);

/**
 * @brief Interprets keyboard input; call it from OnKeyboardInput.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when the input was used. Must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnKeyboardInput(UISliderCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled);

/**
 * @brief Gives its preferred size: the theme's minimum, at least the thumb's height; call it from Measure.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param outSize [out] Receives the size in UI pixels. Must not be NULL.
 */
void UISliderCapability_Measure(const UISliderCapability* self, Widget* widget, Vector2* outSize);

/**
 * @brief Registers the slider's standard widget type. Once per factory (the kit does).
 * @param factory The factory; must not be NULL.
 * @param lookCapabilityId The look's capability id.
 * @param sliderCapabilityId The slider's capability id.
 * @param outTypeId [out] Receives the type's id. Must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UISlider_RegisterType(UIWidgetFactory* factory, uint64_t lookCapabilityId, uint64_t sliderCapabilityId, uint64_t* outTypeId);


// The value's arithmetic.
/**
 * @brief Clamps a value into a range and, with a step, onto the nearest whole step from the lower end. The upper end counts
 *        as a step, so it stays reachable when the range is not a whole number of steps.
 * @param value The value; a NaN gives the lower end.
 * @param minimum The lower end.
 * @param maximum The upper end.
 * @param hasStep Whether it keeps to steps.
 * @param step The step, when @p hasStep; one that is no positive finite number, or too small to count with, is no step.
 * @returns The value.
 */
double UISlider_ClampAndSnap(double value, double minimum, double maximum, bool hasStep, double step);

/**
 * @brief Moves a value by steps: what the arrows and the wheel do.
 *
 * With a step it goes to whole steps from the one it is on (a value between two counts from the next one in that direction), so
 * from an upper end that is no whole step it comes to the last one below it. @p count is rounded to whole steps there: less
 * than half a step moves nothing. Without a step a step is a hundredth of the range. The answer stays in the range.
 * @param value The value; a NaN counts as the lower end.
 * @param minimum The lower end.
 * @param maximum The upper end.
 * @param hasStep Whether it keeps to steps.
 * @param step The step, when @p hasStep; one that is no positive finite number, or too small to count with, is no step.
 * @param count How many steps, up for positive; a NaN is none.
 * @returns The value.
 */
double UISlider_GetStepped(double value, double minimum, double maximum, bool hasStep, double step, double count);

/**
 * @brief Returns where a value lies in its range.
 * @param value The value.
 * @param minimum The lower end.
 * @param maximum The upper end.
 * @returns The share in [0;1]; 0 for an empty range.
 */
double UISlider_GetFraction(double value, double minimum, double maximum);

/**
 * @brief Returns the share of the range a pointer means: the thumb's centre follows the pointer, so its centre travels the
 *        box less the thumb's length, and past the ends the share stays at 0 or 1.
 * @param pointer The pointer along the box, from its start.
 * @param boxLength The box's length.
 * @param thumbLength The thumb's length, in the same unit.
 * @returns The share in [0;1]; 0 when the box is no longer than the thumb.
 */
double UISlider_GetPointerFraction(double pointer, double boxLength, double thumbLength);
