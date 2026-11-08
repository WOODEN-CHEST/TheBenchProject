#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "UITextBox.h"


/*
 * Private to the text box's files: UITextBox.c (its content, its style ranges, its layout and bars, calling code's functions) and
 * UITextBoxInput.c (the keys, the mouse, entering and leaving, the clipboard, the context menu).
 */


// Types.
/** What a drag selects by: characters, words (after a double click), lines (after a triple click). */
typedef enum UITextBoxDragUnitEnum
{
    UITextBoxDragUnit_Character = 1,
    UITextBoxDragUnit_Word = 2,
    UITextBoxDragUnit_Line = 3
} UITextBoxDragUnit;


// Functions (UITextBox.c).
/** The capability of a text box look's widget: right after its Widget. */
UITextBoxCapability* UITextBoxInternal_GetCapability(void* widget);

/** Finds a text box by id; an error for a stale id or a widget that is no text box. */
Error UITextBoxInternal_Resolve(UIScreen* screen, uint64_t widgetId, Widget** outWidget, UITextBoxCapability** outBox);

/** Hears a change of the text: the style ranges move with it, what shows changed, the caret moved and is to be shown. */
void UITextBoxInternal_OnTextChanged(UITextBoxCapability* self, const UITextEditChange* change);

/** Hears the caret or the selection move: the blink restarts and the caret is to be shown. */
void UITextBoxInternal_OnCaretMoved(UITextBoxCapability* self);

/** The look's layout of the text, brought up to date, and how many UI pixels a layout unit is; false without the look's answers. */
bool UITextBoxInternal_GetLayout(UITextBoxCapability* self, Widget* widget, const TextLayout** outLayout, float* outUnitsToUI);

/** A byte offset of the text as an offset of the look's layout (a masked text's count mask characters), and back. */
size_t UITextBoxInternal_ToLayoutOffset(const UITextBoxCapability* self, size_t boxOffset);
size_t UITextBoxInternal_ToBoxOffset(const UITextBoxCapability* self, size_t layoutOffset);

/** Posts what an edit did: Changed (and Committed for a whole change, one made while the box is not entered), the Refused cue when a limit
 *  refused anything. Nothing for an edit that changed nothing, but the cue. */
Error UITextBoxInternal_PostEdit(Widget* widget, UITextBoxCapability* self, const UITextEditChange* change, bool isWhole);

/** Whether the box's text differs from the text at entering. */
bool UITextBoxInternal_HasTextChangedSinceEntering(const UITextBoxCapability* self);

/** Puts back the text at entering (UITextEdit_RevertToMark), moving the style ranges with it; whether it changed. */
Error UITextBoxInternal_RevertToEntered(UITextBoxCapability* self, bool* outHasChanged);


// Functions (UITextBoxInput.c): the kind's hooks.
Error UITextBoxInternal_OnMouseInput(void* widget, const WidgetMouseInputArgs* args);
Error UITextBoxInternal_OnPressLost(void* widget, GameKey button);
Error UITextBoxInternal_OnScrollInput(void* widget, const WidgetScrollInputArgs* args, bool* outIsHandled);
Error UITextBoxInternal_OnKeyboardInput(void* widget, const WidgetKeyboardInputArgs* args, bool* outIsHandled);
Error UITextBoxInternal_OnEnter(void* widget, UIEnterCause cause);
Error UITextBoxInternal_OnLeave(void* widget, UILeaveReason reason);

/** The behaviour's part of a tick for the mouse: a drag past an edge scrolls and selects on, and one whose button is up ends. */
Error UITextBoxInternal_UpdateDrag(void* widget, double passedSeconds);

/** The behaviour's part of a tick for a visit gone on through the box's own context menu: once the menu has closed without giving the
 *  focus back, the visit ends (Committed if the text changed). */
Error UITextBoxInternal_UpdateVisit(void* widget);
