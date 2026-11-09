#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "GameKey.h"
#include "UIElement.h"
#include "UIValue.h"
#include "UIWidget.h"


/**
 * @file UISlider.h
 * @brief The slider element: a value piece (UIValue.h) on a track, across or upright, by a drag, the arrows, the wheel or a middle click.
 *
 * Its value is the value piece's, reached by the UIValue_* functions by widget id like every element holding one; the track is the
 * piece's drag range (its soft range, else its hard limits, else a value list's ends), which a slider needs both ends of. UPRIGHT
 * (UISliderOrientation_Vertical), up is higher: the thumb travels from the bottom to the top.
 *
 * It is an edit element (UIWidget.h, "EDIT ELEMENTS"): moving the focus onto it changes nothing, and its keys reach it only once it is
 * entered, by the accept action (Enter), any press on it or code; the back action (Escape) or the accept action leaves it, and Tab
 * leaves it and goes on. A left press on the track puts the thumb under the pointer; a press on the thumb holds it where it was
 * grabbed, so a click on it changes nothing. A drag follows the pointer past the box, the screen and the window: past an end the value
 * is exactly at the limit. Shift during a drag is a flag on the rate (UISliderDrag): held, the pointer moves the value at the piece's
 * FineFactor of the rate; let go, the rate comes back and the value never jumps; once the pointer passes an end of the box the thumb is
 * under it again. Entered, the arrows step (ui_up and ui_right higher, ui_down and ui_left lower; with Shift a fine step; repeats
 * following; at an end they do nothing and are still its), Home and End go to the track's ends. A middle press let go on it, or the
 * reset action (focused or only under the pointer, entered or not), puts it back to its default. The wheel changes it only while it is
 * entered or with Ctrl: otherwise it goes on up the tree, so scrolling a panel never changes a value by accident.
 *
 * EVENTS: Changed each time the value moves, then Committed at the release or the key's release (each arrow press is a whole change; a
 * wheel notch, a middle click and a reset are whole at once) unless the value ended where it began; Escape during a drag or a held key
 * puts the first value back and posts Cancelled, and the slider stays entered. With no change going on it leaves Escape to the screen,
 * which leaves it: the presses already committed stay. Leaving it any way ends a change in progress with its commit (a cancel by code
 * takes it back), and what is left of that drag or key does nothing. A press that stops reaching it ends the change at once, with its
 * commit. The events carry the value as a double (under a compound's id when the slider is its part). It also posts the events every
 * element shares (UIElement_PostMouseEvents).
 *
 * LOOKS (UIKit_RegisterLook: a struct starting with UISliderLookBase) draw the track and the thumb as they like, reading the state
 * (UISliderCapability_GetState), the share of the track (UISliderCapability_GetShare) and the orientation, and tell the behaviour how
 * long the thumb is along the track (UISliderCapability_SetThumbLength).
 */


// Types.
/**
 * @brief Which way a slider's track runs.
 */
typedef enum UISliderOrientationEnum
{
    /** @brief Across: right is higher. */
    UISliderOrientation_Horizontal,
    /** @brief Upright: up is higher; the thumb travels from the bottom to the top. */
    UISliderOrientation_Vertical
} UISliderOrientation;

/**
 * @brief What a slider is created with (UIKit_CreateSlider). A plain value; build one with UISliderOptions_Create.
 */
typedef struct UISliderOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief The value, its settings (with both ends of a drag range) and its states. */
    UIValueOptions Value;
    /** @brief Which way the track runs. */
    UISliderOrientation Orientation;
} UISliderOptions;

/**
 * @brief What a slider look's Construct gets (UIKit_CreateSlider passes it): the kit and the options.
 */
typedef struct UISliderArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UISliderOptions* Options;
} UISliderArgs;

/**
 * @brief A drag of a slider's thumb, as pure arithmetic in shares of the track: what Shift does to it is one small function
 *        (UISliderDrag_Move). A plain value; start one with UISliderDrag_Begin.
 */
typedef struct UISliderDragStruct
{
    /** @brief Where the thumb is, in [0;1]: built up unsnapped, so a slow drag over a stepped track still moves it. */
    double Share;
    /** @brief The thumb's offset from the pointer, kept while Shift is up. */
    double Offset;
    /** @brief The pointer's share at the last move, not clamped (past an end it goes on below 0 or above 1). */
    double PointerShare;
    /** @brief How far past a share of 0 or 1 the box's own ends lie (half the thumb's length, in shares): beyond it the pointer has
     *  passed an end. */
    double EdgeMargin;
    /** @brief Whether Shift was held at the last move. */
    bool IsFine;
} UISliderDrag;

/**
 * @brief A slider's capability: its value piece, its orientation and what a drag, a held key or a middle press is doing. No look;
 *        embedded by a look's widget type right after its Widget (UISliderLookBase). Its fields are read-only outside the slider's
 *        module, the value piece's outside the value's (calling code reaches it by UIValue_*).
 */
typedef struct UISliderCapabilityStruct
{
    /** @brief The value: what UIValue_* reach. */
    UIValueCapability Value;
    /** @brief Which way the track runs. */
    UISliderOrientation _orientation;
    /** @brief A left press on it is being dragged. */
    bool _isDragging;
    /** @brief The drag in progress. */
    UISliderDrag _drag;
    /** @brief The key whose hold is a change in progress, or GameKey_Unknown. */
    GameKey _sessionKey;
    /** @brief The key whose change was cancelled, or ended by leaving the slider: its repeats and release do nothing until it is pressed
     *  anew. GameKey_Unknown when none. */
    GameKey _endedKey;
    /** @brief A middle press on it is held: let go on it, it resets. */
    bool _isMiddlePressed;
    /** @brief How long the look's thumb is along the track, in the widget's own UI pixels: its centre travels the track less this, and a
     *  press within it holds it. 0 until the look says (a thumb of no length: the pointer is the value). */
    float _thumbLength;
    /** @brief A drag's track is held: the drag range around the value where it began (UIValueSettings_GetDragRangeAround), so a value past
     *  a soft range moves on from where it is. */
    bool _hasDragTrack;
    /** @brief That track's low end, while _hasDragTrack. */
    double _dragTrackMinimum;
    /** @brief Its high end. */
    double _dragTrackMaximum;
} UISliderCapability;

/**
 * @brief How every slider look's widget struct starts: the widget, then the capability.
 */
typedef struct UISliderLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The slider's behaviour. */
    UISliderCapability Slider;
} UISliderLookBase;


// Helpers.
/**
 * @brief Default options: across, the range [0, 1] at 0, no step, enabled, no default.
 * @returns The options.
 */
static inline UISliderOptions UISliderOptions_Create(void)
{
    return (UISliderOptions)
    {
        .Element = UIElementOptions_Create(),
        .Value = UIValueOptions_Create(),
        .Orientation = UISliderOrientation_Horizontal
    };
}


// Calling code: by widget id (the value through UIValue_*).
/**
 * @brief Turns a slider's track across or upright.
 * @param screen The slider's screen; must not be NULL.
 * @param widgetId The slider.
 * @param orientation Which way.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no slider; ErrorCode_ArgumentOutOfRange for a value that
 *          is no orientation.
 */
Error UISlider_SetOrientation(UIScreen* screen, uint64_t widgetId, UISliderOrientation orientation);

/**
 * @brief Reads which way a slider's track runs.
 * @param screen The slider's screen; must not be NULL.
 * @param widgetId The slider.
 * @param outOrientation [out] Receives it; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no slider.
 */
Error UISlider_GetOrientation(UIScreen* screen, uint64_t widgetId, UISliderOrientation* outOrientation);


// The drag (pure).
/**
 * @brief Starts a drag.
 * @param self The drag; must not be NULL.
 * @param share Where the thumb starts, in [0;1] (clamped).
 * @param offset The thumb's offset from the pointer: the thumb's share less the pointer's for a press on the thumb, 0 for a press on the
 *        track (the thumb under the pointer).
 * @param pointerShare The pointer's share, not clamped.
 * @param edgeMargin How far past 0 and 1 the box's ends lie, in shares (>= 0).
 * @param isFine Whether Shift is held.
 */
void UISliderDrag_Begin(UISliderDrag* self, double share, double offset, double pointerShare, double edgeMargin, bool isFine);

/**
 * @brief Moves a drag to the pointer's new share: the one small function Shift's behaviour lives in.
 *
 * Normally the thumb is the pointer plus the offset. While Shift is held the thumb moves by the pointer's motion times @p fineFactor,
 * and at each change of Shift the offset becomes the thumb less the pointer (at the pointer's last place), so the value never jumps.
 * With Shift up, a thumb held at an end makes the offset give way (turning back moves it at once), and once the pointer is past that
 * end of the box too, the offset is 0: the thumb is under the pointer again. The thumb's share builds up unsnapped; whoever reads it
 * snaps the value.
 *
 * Blender's way, which may replace this one: the offset would keep what the press gave it, never updated at Shift's changes, so letting
 * Shift go would put the thumb where the pointer and that offset say, a jump.
 * @param self The drag; must not be NULL.
 * @param pointerShare The pointer's share, not clamped; a value that is not finite counts as its last one.
 * @param isFine Whether Shift is held.
 * @param fineFactor How much Shift slows it, within (0, 1].
 * @returns The thumb's share, in [0;1].
 */
double UISliderDrag_Move(UISliderDrag* self, double pointerShare, bool isFine, double fineFactor);


// Behaviour helpers, for looks and tests.
/**
 * @brief Constructs the capability from the kind's arguments.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The arguments; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for value settings that break a rule (both ends of a drag range are needed), a value that
 *          is not finite or an orientation that is none; otherwise the options' error.
 */
Error UISliderCapability_Construct(UISliderCapability* self, Widget* widget, const UISliderArgs* args);

/**
 * @brief Releases the capability.
 * @param self The capability; must not be NULL.
 */
void UISliderCapability_Deconstruct(UISliderCapability* self);

/**
 * @brief The behaviour's part of a tick: ends a drag or a key's change whose release cannot reach the slider any more.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_Update(UISliderCapability* self, Widget* widget);

/**
 * @brief Returns the state a look shows for the slider: disabled, pressed (a drag), entered, hovered, focused or normal.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns The state.
 */
UIElementState UISliderCapability_GetState(const UISliderCapability* self, const Widget* widget);

/**
 * @brief Returns where the thumb is along the track, through the value's conversion: on the drag range around the value (a value past a
 *        soft range shows at the end it passed), during a drag on the track the drag holds.
 * @param self The capability; must not be NULL.
 * @returns The share in [0;1], 0 at the left or the bottom.
 */
double UISliderCapability_GetShare(const UISliderCapability* self);

/**
 * @brief Returns which way the track runs.
 * @param self The capability; must not be NULL.
 * @returns The orientation.
 */
static inline UISliderOrientation UISliderCapability_GetOrientation(const UISliderCapability* self)
{
    return self->_orientation;
}

/**
 * @brief Tells the behaviour how long the look's thumb is along the track; call it from the look's Update when it can change.
 * @param self The capability; must not be NULL.
 * @param length The length in the widget's own UI pixels; a value that is not a finite number >= 0 counts as 0.
 */
void UISliderCapability_SetThumbLength(UISliderCapability* self, float length);

/**
 * @brief Interprets mouse input.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnMouseInput(UISliderCapability* self, Widget* widget, const WidgetMouseInputArgs* args);

/**
 * @brief Ends a drag whose press the screen says is lost: the value stays where it got to, and the change is committed. A middle press
 *        lost resets nothing.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param button The press's mouse button.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnPressLost(UISliderCapability* self, Widget* widget, GameKey button);

/**
 * @brief Interprets the wheel, which it takes only while entered or with Ctrl (Shift for a fine step).
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when it takes the wheel. Must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnScrollInput(UISliderCapability* self, Widget* widget, const WidgetScrollInputArgs* args, bool* outIsHandled);

/**
 * @brief Ends the change in progress when the slider is left.
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
 * @brief Interprets keyboard input.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The input; must not be NULL.
 * @param outIsHandled [out] Set when the input was used. Must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UISliderCapability_OnKeyboardInput(UISliderCapability* self, Widget* widget, const WidgetKeyboardInputArgs* args,
    bool* outIsHandled);
