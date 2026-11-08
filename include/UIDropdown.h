#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIChoice.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIDropdown.h
 * @brief The dropdown element: one choice of several (UIChoice.h), a head showing it (or the mixed text) and an arrow, and a list that
 *        opens as a popup (UIPopup.h).
 *
 * THE HEAD is a press element (UIWidget.h, "EDIT ELEMENTS"): focused, it hears every key first, but only Enter and Space mean
 * something to it, so the arrows go past it. A left press on it opens the list at once (another one closes it), so a press, a drag to a
 * row and a release there chooses that row (a release within a click's distance of the press chooses nothing: a list placed over its
 * head is not chosen by the click that opened it); Enter or Space opens it too. Ctrl with the wheel over it steps through the options while it
 * is closed (a whole change each notch); a plain wheel goes on up the tree, so a page scrolls past it. Disabled, it swallows presses and
 * is not focusable.
 *
 * THE LIST opens below the head, or above it when there is too little room below and more above (Direction Auto; Up and Down force a
 * side), as wide as the head at least, VisibleCount rows tall at most, shorter when the room is. It shows widgets for the rows in view
 * only (UIRowRecycler.h), so a provider's hundred thousand options cost what twenty rows cost. With more rows than fit it scrolls, by the
 * wheel, by the keys and by a scroll bar at its side, with strips at its top and bottom whose arrows show while more lies that way (a
 * press on one scrolls a row, holding it goes on). It is an edit element entered as it opens, so its keys work at once: Up and Down move
 * the highlight, Home and End go to the ends, Page Up and Page Down a list's height, a letter to the next option starting with it, Enter
 * chooses the highlighted option, Tab closes it, and Escape closes it (the popup layer). The highlight starts at the selected option
 * and follows the pointer when it moves (rows scrolled under a pointer at rest leave it where the keys put it). A row chosen by a
 * click, a release over it, or Enter selects it: UIElementEvent_Changed then UIElementEvent_Committed on the head with the index as a
 * double (nothing when it was selected already and not mixed), and the list closes at its next update, giving the focus back to the
 * head (the row stays under the pointer until the screen has delivered the release, so what lies behind hears nothing of it). A press
 * outside closes it and goes on.
 *
 * Calling code uses the UIChoice_* functions for the options and the selection and the UIDropdown_* functions below, by widget id. The
 * head's look (UIKit_RegisterLook: a struct starting with UIDropdownLookBase) draws it from UIDropdownCapability_GetShownText and
 * UIDropdownCapability_GetState. The open list and its rows are the kit's own widgets, drawn from the theme's dropdown kind with the
 * variant of the head's look; their look is not replaceable yet.
 */


// Forward declarations.
/** @brief The widget factory; full type in UIWidgetFactory.h. */
typedef struct UIWidgetFactoryStruct UIWidgetFactory;


// Macros.
/** @brief How many rows a list shows unless its options say otherwise. */
#define UI_DROPDOWN_DEFAULT_VISIBLE_COUNT ((size_t)8)
/** @brief The most rows a list may be made to show. */
#define UI_DROPDOWN_MAX_VISIBLE_COUNT ((size_t)100)
/** @brief How long a held press on a scroll strip waits before it scrolls again, and how often after that, in seconds. */
#define UI_DROPDOWN_STRIP_DELAY_SECONDS 0.3
#define UI_DROPDOWN_STRIP_REPEAT_SECONDS 0.06
/** @brief How many rows a notch of the wheel scrolls the list. */
#define UI_DROPDOWN_WHEEL_ROWS 3.0f


// Types.
/**
 * @brief Which side a dropdown's list opens on.
 */
typedef enum UIDropdownDirectionEnum
{
    /** @brief Below, unless there is too little room below and more above. */
    UIDropdownDirection_Auto,
    /** @brief Above, always. */
    UIDropdownDirection_Up,
    /** @brief Below, always. */
    UIDropdownDirection_Down
} UIDropdownDirection;

/**
 * @brief What a dropdown is created with (UIKit_CreateDropdown). A plain value; build one with UIDropdownOptions_Create.
 */
typedef struct UIDropdownOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief The options' texts as UTF-8 (copied), or NULL for none (a provider can be given later, UIChoice_SetProvider). */
    const unsigned char* const* Options;
    /** @brief How many options, at most UI_CHOICE_MAX_OPTIONS. */
    size_t OptionCount;
    /** @brief Whether an option is selected. */
    bool HasSelection;
    /** @brief The selected option, when HasSelection; below OptionCount. */
    size_t Selected;
    /** @brief How many rows the open list shows at most; within [1, UI_DROPDOWN_MAX_VISIBLE_COUNT]. */
    size_t VisibleCount;
    /** @brief Which side the list opens on. */
    UIDropdownDirection Direction;
    /** @brief Whether it takes input. */
    bool IsEnabled;
    /** @brief The text the head shows while the choice is mixed (UTF-8, copied), or NULL for a hyphen, "-". */
    const unsigned char* MixedText;
} UIDropdownOptions;

/**
 * @brief What a dropdown look's Construct gets (UIKit_CreateDropdown passes it): the kit and the options.
 */
typedef struct UIDropdownArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIDropdownOptions* Options;
} UIDropdownArgs;

/**
 * @brief A dropdown's capability: its choice and its logical state, no look. Embedded by a look's widget type right after its Widget
 *        (UIDropdownLookBase); its fields are read-only outside the dropdown's module.
 */
typedef struct UIDropdownCapabilityStruct
{
    /** @brief The options and the selection, which calling code reaches by UIChoice_*. */
    UIChoiceCapability Choice;
    /** @brief The press behaviour: it opens at the press, by Enter or Space too. */
    UIPressable _press;
    /** @brief How many rows the list shows at most. */
    size_t _visibleCount;
    /** @brief Which side the list opens on. */
    UIDropdownDirection _direction;
    /** @brief Whether it takes input. */
    bool _isEnabled;
    /** @brief The text shown while mixed. */
    UITextContent _mixedText;
    /** @brief What the head shows: the selected option's text, the mixed text, or nothing. */
    UITextContent _shownText;
    /** @brief The choice's revision _shownText was made from. */
    uint64_t _shownRevision;
    /** @brief Scratch for reading an option's text. Owned. */
    GenericBuffer _scratch;
    /** @brief The open list (a popup's root), or WIDGET_ID_INVALID while closed. */
    uint64_t _listId;
} UIDropdownCapability;

/**
 * @brief How every dropdown look's widget struct starts: the widget, then the capability.
 */
typedef struct UIDropdownLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The dropdown's behaviour. */
    UIDropdownCapability Dropdown;
} UIDropdownLookBase;


// Helpers.
/**
 * @brief Default options: no options, nothing selected, eight rows, opening by the room, enabled, a hyphen for mixed.
 * @returns The options.
 */
static inline UIDropdownOptions UIDropdownOptions_Create(void)
{
    return (UIDropdownOptions)
    {
        .Element = UIElementOptions_Create(),
        .Options = NULL,
        .OptionCount = 0U,
        .HasSelection = false,
        .Selected = 0U,
        .VisibleCount = UI_DROPDOWN_DEFAULT_VISIBLE_COUNT,
        .Direction = UIDropdownDirection_Auto,
        .IsEnabled = true,
        .MixedText = NULL
    };
}


// Calling code: by widget id.
/**
 * @brief Opens a dropdown's list, entered, as Enter does; nothing happens when it is open, disabled or has no options.
 * @param screen The dropdown's screen; must not be NULL.
 * @param widgetId The dropdown.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dropdown; otherwise an error from making the list or
 *          opening the popup.
 */
Error UIDropdown_Open(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Closes a dropdown's list without choosing; nothing happens when it is closed.
 * @param screen The dropdown's screen; must not be NULL.
 * @param widgetId The dropdown.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dropdown; otherwise a hook's error.
 */
Error UIDropdown_Close(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Reads whether a dropdown's list is open, and which widget it is (its rows are its children's children).
 * @param screen The dropdown's screen; must not be NULL.
 * @param widgetId The dropdown.
 * @param outIsOpen [out] Receives whether it is open. Must not be NULL.
 * @param outListId [out] Receives the list's id, or WIDGET_ID_INVALID; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dropdown.
 */
Error UIDropdown_IsOpen(UIScreen* screen, uint64_t widgetId, bool* outIsOpen, uint64_t* outListId);

/**
 * @brief Sets how many rows a dropdown's list shows at most, from its next opening on.
 * @param screen The dropdown's screen; must not be NULL.
 * @param widgetId The dropdown.
 * @param visibleCount Within [1, UI_DROPDOWN_MAX_VISIBLE_COUNT].
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dropdown; ErrorCode_ArgumentOutOfRange for a count
 *          out of bounds, which changes nothing.
 */
Error UIDropdown_SetVisibleCount(UIScreen* screen, uint64_t widgetId, size_t visibleCount);

/**
 * @brief Sets which side a dropdown's list opens on, from its next opening on.
 * @param screen The dropdown's screen; must not be NULL.
 * @param widgetId The dropdown.
 * @param direction The side.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dropdown; ErrorCode_ArgumentOutOfRange for an
 *          unknown direction, which changes nothing.
 */
Error UIDropdown_SetDirection(UIScreen* screen, uint64_t widgetId, UIDropdownDirection direction);

/**
 * @brief Enables or disables a dropdown. Disabling it closes its list and takes its focus away.
 * @param screen The dropdown's screen; must not be NULL.
 * @param widgetId The dropdown.
 * @param isEnabled Whether it takes input.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dropdown; otherwise a hook's error.
 */
Error UIDropdown_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);


// Behaviour helpers, for looks.
/**
 * @brief Returns what the head shows: the selected option's text, the mixed text while mixed, or nothing; kept current by the behaviour's
 *        update (a look's Update runs after it).
 * @param self The capability; must not be NULL.
 * @returns The text content.
 */
static inline const UITextContent* UIDropdownCapability_GetShownText(const UIDropdownCapability* self)
{
    return &self->_shownText;
}

/**
 * @brief Returns the state a look shows the head in: disabled, pressed (held, or its list open), hovered, focused or normal.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns The state.
 */
UIElementState UIDropdownCapability_GetState(const UIDropdownCapability* self, const Widget* widget);

/**
 * @brief Registers the kit's own widget types a dropdown opens: the list and its rows. The kit calls it when it is constructed.
 * @param kit The kit, whose capabilities exist already; must not be NULL.
 * @param factory The kit's factory; must not be NULL.
 * @returns Success, or the factory's error.
 */
Error UIDropdown_RegisterTypes(UIKit* kit, UIWidgetFactory* factory);
