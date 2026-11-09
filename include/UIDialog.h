#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UIElement.h"
#include "UIWidget.h"


/**
 * @file UIDialog.h
 * @brief The modal dialog (kind dialog): a title, a message, content calling code adds, and a row of buttons, opened over a backdrop that
 *        takes every press, answered by a button, Enter or Escape.
 *
 * MAKING ONE. UIKit_CreateDialog makes it whole but shut: its title and message (labels), an empty column for content
 * (UIDialog_GetContentId: fields, a list, anything) and its buttons (from a list, each with an answer id, at most one the default and
 * one the cancel). Calling code adds its content and subscribes to the dialog's id, then opens it (UIDialog_Open), with the focus on a
 * field (entered at once: an edit element's keys work as it opens) or else on the default button when it can take the focus, else the first
 * button that can. A dialog is opened once: closed, it is destroyed (UIPopup.h), so each question makes a new one.
 *
 * ANSWERING. A button's activation (its click mode, Enter or Space on it) answers with that button's id. Enter answers with the default
 * button's and Escape with the cancel button's, IN ONE PRESS EVEN FROM INSIDE AN ENTERED FIELD: the dialog is the field's ancestor, so it
 * hears what the field leaves unused before the screen would leave the field; it leaves the field itself first (Enter keeping what was
 * typed, Escape cancelling it), so the field's own events come first. Enter on a field that is only focused enters it (the screen takes
 * that press before any ancestor). With no cancel button Escape answers nothing: it leaves an entered field, and is otherwise swallowed (the
 * dialog stays, the game does not get it). A disabled default or cancel button is as none: a rename's OK disabled until a name is typed
 * answers nothing. The answer is UIElementEvent_Activated under the dialog's id, the answer id its index (UIEvent.HasIndex, UIEvent.Index);
 * the dialog closes at the first update after that event's delivery, so a handler of the answer still reads the fields by id. It closes
 * alone (UIScreen_ClosePopupAndItsOwn): a popup its answer opened (the next question) stays.
 *
 * It stays modal: nothing behind it takes a press, the wheel or hover, and while it is the topmost popup the focus stays inside it (a
 * dialog that finds the focus elsewhere puts it on its default button, or the first that can take it; with none, it leaves the focus). It closes with no answer when its owner goes (UIDialogOptions.OwnerId)
 * or by code (UIDialog_Close); its owner hears UIEventKind_PopupClosed either way, as a popup's owner does.
 *
 * Its look (UIKit_RegisterLook: a struct starting with UIDialogLookBase) draws its background, tells the behaviour the theme's metrics
 * (UIDialogCapability_SetMetrics) and measures through it (UIDialog_Measure); the dialog lays its parts out.
 */


// Forward declarations.
/** @brief The widget factory; full type in UIWidgetFactory.h. */
typedef struct UIWidgetFactoryStruct UIWidgetFactory;


// Macros.
/** @brief The most buttons a dialog has. */
#define UI_DIALOG_MAX_BUTTONS ((size_t)8)


// Types.
/**
 * @brief One of a dialog's buttons. A plain value.
 */
typedef struct UIDialogButtonStruct
{
    /** @brief The answer it gives: the index of the dialog's Activated. Any value; ids of one dialog differ. */
    int32_t Id;
    /** @brief Its text, UTF-8; copied. Must not be NULL. */
    const unsigned char* Text;
    /** @brief Whether Enter answers with it; at most one button. */
    bool IsDefault;
    /** @brief Whether Escape answers with it; at most one button. */
    bool IsCancel;
} UIDialogButton;

/**
 * @brief What a dialog is created with (UIKit_CreateDialog). A plain value; build one with UIDialogOptions_Create.
 */
typedef struct UIDialogOptionsStruct
{
    /** @brief What every element is created with: its variant, look and scale (its place is the popup layer's: centred on the surface). */
    UIElementOptions Element;
    /** @brief The title, UTF-8 (copied), or NULL for none. */
    const unsigned char* Title;
    /** @brief The message, UTF-8 (copied), wrapped at the theme's width, or NULL for none. */
    const unsigned char* Message;
    /** @brief The buttons, ButtonCount of them, in the order they stand (left to right); borrowed for the call. */
    const UIDialogButton* Buttons;
    /** @brief How many, within [1, UI_DIALOG_MAX_BUTTONS]. */
    size_t ButtonCount;
    /** @brief The widget it belongs to: told when it closes, and it closes (unanswered) when the widget goes; WIDGET_ID_INVALID for none. */
    uint64_t OwnerId;
} UIDialogOptions;

/**
 * @brief What a dialog look's Construct gets (UIKit_CreateDialog passes it): the kit and the options.
 */
typedef struct UIDialogArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UIDialogOptions* Options;
} UIDialogArgs;

/**
 * @brief The theme's sizes a dialog lays itself out by, in its UI pixels; its look tells them (UIDialogCapability_SetMetrics).
 */
typedef struct UIDialogMetricsStruct
{
    /** @brief Inside its box, horizontal and vertical. */
    Vector2 Padding;
    /** @brief Between the title, the message, the content and the buttons. */
    float Spacing;
    /** @brief Between two buttons. */
    float ButtonSpacing;
    /** @brief The least width of a button. */
    float ButtonWidth;
    /** @brief Its least size. */
    Vector2 MinSize;
    /** @brief The widest it grows to fit its message, which wraps past it. */
    float MaxWidth;
} UIDialogMetrics;

/**
 * @brief A dialog's capability: its parts, its buttons' answers and its state, no look. Embedded by a look's widget type right after its
 *        Widget (UIDialogLookBase); its fields are read-only outside the dialog's module.
 */
typedef struct UIDialogCapabilityStruct
{
    /** @brief Its title, message and content, made with it; WIDGET_ID_INVALID until then. */
    uint64_t _titleId;
    uint64_t _messageId;
    uint64_t _contentId;
    /** @brief Its buttons and their answers, _buttonCount of each. */
    uint64_t _buttonIds[UI_DIALOG_MAX_BUTTONS];
    int32_t _answers[UI_DIALOG_MAX_BUTTONS];
    size_t _buttonCount;
    /** @brief The default button's and the cancel button's places among them, or -1. */
    int32_t _defaultIndex;
    int32_t _cancelIndex;
    /** @brief Whether it has a title and a message to show. */
    bool _hasTitle;
    bool _hasMessage;
    /** @brief Its owner, for the popup. */
    uint64_t _ownerId;
    /** @brief Whether it was opened, whether it was answered, and whether its answer was delivered (it closes at its next update). */
    bool _isOpened;
    bool _isAnswered;
    bool _isClosePending;
    /** @brief The sizes it lays itself out by. */
    UIDialogMetrics _metrics;
} UIDialogCapability;

/**
 * @brief How every dialog look's widget struct starts: the widget, then the capability.
 */
typedef struct UIDialogLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The dialog's behaviour. */
    UIDialogCapability Dialog;
} UIDialogLookBase;


// Helpers.
/**
 * @brief Options with a title and no buttons yet (give at least one): no message, no owner.
 * @param title The title (borrowed until the dialog is made), or NULL.
 * @returns The options.
 */
static inline UIDialogOptions UIDialogOptions_Create(const unsigned char* title)
{
    return (UIDialogOptions){ .Element = UIElementOptions_Create(), .Title = title, .Message = NULL, .Buttons = NULL, .ButtonCount = 0U,
        .OwnerId = 0U };
}


// Calling code: by widget id.
/**
 * @brief Opens a dialog made with UIKit_CreateDialog: a modal popup centred on the surface, the focus on @p focusId (entered if it is an
 *        edit element) or, given WIDGET_ID_INVALID, on the default button, else the first.
 * @param screen The dialog's screen; must not be NULL.
 * @param widgetId The dialog.
 * @param focusId A focusable widget inside the dialog, or WIDGET_ID_INVALID.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no dialog, or one opened already; otherwise the popup
 *          layer's error.
 */
Error UIDialog_Open(UIScreen* screen, uint64_t widgetId, uint64_t focusId);

/**
 * @brief Closes a dialog with no answer, at once (it is destroyed), with the popups opened from inside it and not others
 *        (UIScreen_ClosePopupAndItsOwn); a dialog made but never opened is destroyed too.
 * @param screen The dialog's screen; must not be NULL.
 * @param widgetId The dialog.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dialog; otherwise a hook's error.
 */
Error UIDialog_Close(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Answers a dialog by code, as if the button with that answer was chosen: Activated with it, and the dialog closes at the first
 *        update after that event's delivery. Nothing happens to a dialog answered already.
 * @param screen The dialog's screen; must not be NULL.
 * @param widgetId The dialog.
 * @param answer A button's answer id.
 * @returns Success; ErrorCode_InvalidOperation for a stale id, a widget that is no dialog, or one not open; ErrorCode_ArgumentOutOfRange for
 *          an answer no button gives.
 */
Error UIDialog_Answer(UIScreen* screen, uint64_t widgetId, int32_t answer);

/**
 * @brief Reads the column calling code adds a dialog's content to.
 * @param screen The dialog's screen; must not be NULL.
 * @param widgetId The dialog.
 * @param outContentId [out] Receives the column (a stack). Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dialog.
 */
Error UIDialog_GetContentId(UIScreen* screen, uint64_t widgetId, uint64_t* outContentId);

/**
 * @brief Reads the button that gives an answer, a button of the kit (UIButton_*: its text, its icon).
 * @param screen The dialog's screen; must not be NULL.
 * @param widgetId The dialog.
 * @param answer The answer id.
 * @param outButtonId [out] Receives the button. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dialog; ErrorCode_ArgumentOutOfRange for an answer no
 *          button gives.
 */
Error UIDialog_GetButtonId(UIScreen* screen, uint64_t widgetId, int32_t answer, uint64_t* outButtonId);


// Behaviour helpers, for looks.
/**
 * @brief Tells the behaviour the theme's sizes it lays the dialog out by; a look calls it from its Update. Values that are not finite numbers
 *        >= 0 count as 0.
 * @param self The capability; must not be NULL.
 * @param widget The dialog's widget; must not be NULL.
 * @param metrics The sizes; must not be NULL.
 */
void UIDialogCapability_SetMetrics(UIDialogCapability* self, Widget* widget, const UIDialogMetrics* metrics);

/**
 * @brief Measures a dialog as its look would: its parts at the width it takes (as wide as its widest part, within its least size and the
 *        widest it grows, its message wrapped there) in a column, its buttons in a row, inside its padding, in its own UI pixels.
 * @param widget The dialog's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param availableWidth The width available, in its UI pixels.
 * @param outSize [out] Receives the size. Must not be NULL.
 * @returns Success, or a part's Measure error.
 */
Error UIDialog_Measure(Widget* widget, const UIMeasureContext* context, float availableWidth, Vector2* outSize);

/**
 * @brief Gives a dialog its parts, which UIKit_CreateDialog made: its title, message and content, and its buttons with their answers.
 * @param screen The dialog's screen; must not be NULL.
 * @param widgetId The dialog.
 * @param titleId The title's label.
 * @param messageId The message's label.
 * @param contentId The content's column.
 * @param buttonIds The buttons, @p buttonCount of them.
 * @param buttons The options' buttons, the same count.
 * @param buttonCount How many.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no dialog; otherwise the subscription's error.
 */
Error UIDialog_SetParts(UIScreen* screen, uint64_t widgetId, uint64_t titleId, uint64_t messageId, uint64_t contentId,
    const uint64_t* buttonIds, const UIDialogButton* buttons, size_t buttonCount);

/**
 * @brief Returns whether a dialog's options can be made: one to UI_DIALOG_MAX_BUTTONS buttons, each with a text, their ids different, at most
 *        one default and one cancel.
 * @param options The options; must not be NULL.
 * @returns true if valid.
 */
bool UIDialogOptions_IsValid(const UIDialogOptions* options);
