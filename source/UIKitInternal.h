#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIKit.h"
#include "UIWidget.h"
#include "UIWidgetFactory.h"


/*
 * Private to the kit and its element kinds (UIKit.c, UIPanel.c, UIButton.c, UIToggle.c, UISlider.c): how the kit composes a widget
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
