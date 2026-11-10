#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIKit.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"


/*
 * Private to the kit and its element kinds (UIKit.c and each kind's module): how the kit composes a widget
 * type from a kind's behaviour and a look.
 *
 * A KIND gives its behaviour as data: its capability's place in a look's struct (right after the Widget), the capability calling
 * code resolves, the flags its widgets start with, and its hooks. Each hook takes the whole widget and finds the capability at
 * that place. The kit's one composed vtable calls, per slot, the kind's hook and the look's: Update runs the kind's then the
 * look's; Layout is the look's when it has one, else the kind's; input, entering, leaving and hover are the kind's alone; Render,
 * Measure and HitTest are the look's.
 */


// Macros.
/** The most capabilities a kind declares. */
#define UI_KIND_MAX_CAPABILITIES ((size_t)3)


// Types.
/** One capability a kind declares, and how to find it in any look's widget of the kind (it sits at the same place in all of them). */
typedef struct UIKindCapabilityStruct
{
    UIElementCapability Capability;
    WidgetCapabilityResolver Resolve;
} UIKindCapability;

/** What a kind of the kit is made of, apart from its look. Static data; every hook is optional. */
typedef struct UIKindBehaviourStruct
{
    /** The kind. */
    UIElementKind Kind;
    /** The capabilities calling code resolves (the panel has none: only its creation); static, CapabilityCount of them. */
    const UIKindCapability* Capabilities;
    /** How many, at most UI_KIND_MAX_CAPABILITIES. */
    size_t CapabilityCount;
    /** The size of the Widget and the capability together: a look's struct is at least this large. */
    size_t BaseSize;
    /** The flags its widgets start with. */
    bool IsFocusable;
    bool NeedsEntering;
    bool EntersItself;

    /** Builds the capability from the kind's arguments (UIButtonArgs, ...) and applies the element's options. On a failure it
     *  leaves nothing to release. */
    Error (*Construct)(void* widget, void* args);
    /** Releases the capability. */
    Error (*Deconstruct)(void* widget);
    /** The behaviour's part of a tick, before the look's Update. */
    Error (*Update)(void* widget, ProgramTime time);
    /** Lays its children out when the look has no Layout of its own. */
    Error (*Layout)(void* widget, const UILayoutContext* context);
    Error (*OnHoverStart)(void* widget, const WidgetHoverArgs* args);
    Error (*OnMouseInput)(void* widget, const WidgetMouseInputArgs* args);
    Error (*OnPressLost)(void* widget, GameKey button);
    Error (*OnScrollInput)(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled);
    Error (*OnKeyboardInput)(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled);
    Error (*OnEnter)(void* widget, UIEnterCause cause);
    Error (*OnLeave)(void* widget, UILeaveReason reason);
    Error (*OnDescendantFocus)(void* widget, uint64_t widgetId);
} UIKindBehaviour;


/** What a window's edge, a widget type of the kit's own, is made with (UIKit_CreateWindow): its window and which edge it is. */
typedef struct UIWindowEdgeArgsStruct
{
    uint64_t WindowId;
    UIWindowEdge Edge;
} UIWindowEdgeArgs;


// The kinds' behaviours (each in its kind's module).
/** The panel's: its options (direction, padding, spacing); children laid out with its own padding or none. */
const UIKindBehaviour* UIPanel_GetKindBehaviour(void);
/** The button's. */
const UIKindBehaviour* UIButton_GetKindBehaviour(void);
/** The toggle's. */
const UIKindBehaviour* UIToggle_GetKindBehaviour(void);
/** The slider's. */
const UIKindBehaviour* UISlider_GetKindBehaviour(void);
/** The scroll bar's. */
const UIKindBehaviour* UIScrollbar_GetKindBehaviour(void);
/** The scroll area's: its children laid out at its offset, the wheel, focus scrolled into view. */
const UIKindBehaviour* UIScrollArea_GetKindBehaviour(void);
/** The dropdown's head: it opens and closes its list. */
const UIKindBehaviour* UIDropdown_GetKindBehaviour(void);
/** The menu bar's: which entry's menu is open. */
const UIKindBehaviour* UIMenuBar_GetKindBehaviour(void);
/** The status bar's: its message's time, what it shows, its sections laid out. */
const UIKindBehaviour* UIStatusBar_GetKindBehaviour(void);
/** The text box's: its text, its caret and selection, its keys and mouse, its bars and the caret kept in view. */
const UIKindBehaviour* UITextBox_GetKindBehaviour(void);
/** The property box's: showing, scrubbing, and typing in a text box of its own. */
const UIKindBehaviour* UIPropertyBox_GetKindBehaviour(void);
/** The labelled element's: its label and element laid out. */
const UIKindBehaviour* UILabelled_GetKindBehaviour(void);
/** The form's: its rows laid out, the labelled ones lined up. */
const UIKindBehaviour* UIForm_GetKindBehaviour(void);
/** The compound slider's: its parts laid out, its value kept in step with theirs. */
const UIKindBehaviour* UICompoundSlider_GetKindBehaviour(void);
/** The row of fields': its fields laid out after their tags. */
const UIKindBehaviour* UIVectorRow_GetKindBehaviour(void);
/** The list's: its data, its selection, its rows recycled, its keys and mouse. */
const UIKindBehaviour* UIList_GetKindBehaviour(void);
/** The tree's: its data walked into rows, its selection by node, its rows recycled, expanding, its keys and mouse. */
const UIKindBehaviour* UITree_GetKindBehaviour(void);
/** The dialog's: its parts laid out, its answer by a button, Enter or Escape, the focus kept inside it. */
const UIKindBehaviour* UIDialog_GetKindBehaviour(void);
/** The colour picker's: its swatch opening its popup. */
const UIKindBehaviour* UIColorPicker_GetKindBehaviour(void);
/** The tool bar's: its items, its buttons' clicks turned into its events, its buttons laid out. */
const UIKindBehaviour* UIToolbar_GetKindBehaviour(void);
/** The window's: its box kept reachable and at least its least size, its layer, its parts laid out, the focus it remembers. */
const UIKindBehaviour* UIWindow_GetKindBehaviour(void);
/** The workspace's: its windows and its dock, its bars and its dock laid out, the drag over it, its layouts. */
const UIKindBehaviour* UIWorkspace_GetKindBehaviour(void);
/** The render view's: its record of the pointer, the buttons and the wheel, a hidden-cursor drag on the presses it names. */
const UIKindBehaviour* UIRenderView_GetKindBehaviour(void);

/** The parts UIKit_CreateWindow made a window of, by id: its frame, its title bar, its caption buttons (by UIWindowButton), its scroll
 *  area, its content's column and its edges (by UIWindowEdge). */
typedef struct UIWindowPartsStruct
{
    uint64_t FrameId;
    uint64_t TitleBarId;
    uint64_t ButtonIds[UIWindowButton_Count];
    uint64_t AreaId;
    uint64_t ContentId;
    uint64_t EdgeIds[UIWindowEdge_Count];
} UIWindowParts;

/** Gives a window its parts once (a second call is refused with ErrorCode_InvalidOperation), subscribes to its buttons' events, posts its
 *  Open cue and applies its state. ErrorCode_InvalidOperation also for a stale id or a widget that is no window. */
Error UIWindow_SetParts(UIScreen* screen, uint64_t windowId, const UIWindowParts* parts);
