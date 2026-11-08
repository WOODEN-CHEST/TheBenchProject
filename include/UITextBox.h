#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
// The text piece, the scrollable piece, a text style per range and a text layout are part of the capability and of what a look answers.
#include "TextLayout.h"
#include "TextStyle.h"
#include "UIElement.h"
#include "UIScrollable.h"
#include "UITextEdit.h"
#include "UIWidget.h"


/**
 * @file UITextBox.h
 * @brief The text box element: text to type in, or to read and select, on one line or several, with limits, a caret and a selection, the
 *        clipboard, an undo of its own, scrolling, a placeholder, a masked mode for passwords and style ranges set by code.
 *
 * AN EDIT ELEMENT (UIWidget.h, "EDIT ELEMENTS"): focused, it hears only Enter and the short list (reset, copy, paste, the menu key), so typing
 * on it does nothing; Enter, a press on it or code enters it. Entered by a key, everything is selected on one line and the caret goes to the
 * end on several; by a press, the caret goes where it landed (a double or a triple click goes on to select); by code, it stays where it was.
 * Entered, it hears every key first: what edits and what moves (the arrows, with Ctrl by words and with Shift extending the selection, Home
 * and End of the line as shown, Ctrl+Home and Ctrl+End, the page keys, Backspace and Delete, with Ctrl by words, Insert for overwrite), the
 * clipboard, undo and select-all actions (tbp:ui_copy, tbp:ui_cut, tbp:ui_paste, tbp:ui_undo, tbp:ui_redo, tbp:ui_select_all), plain Enter
 * on a box that allows line breaks (refused at the line limit) and Tab on one that accepts tabs. Before any action it claims the presses
 * that type text: a letter alone or with Shift, with Ctrl and Alt together, or with the right Alt alone (AltGr), so an action bound to a
 * letter types the letter and AltGr's characters never select all or paste. It leaves Up, Down and the page keys on a box that shows one
 * line (one that holds no line break and does not wrap) and Ctrl+Enter to whoever is around it (a console's history, a dialog's default),
 * and what nobody takes is the screen's: Enter keeps and leaves, Escape cancels and leaves, Tab keeps and goes on. Left any way, a visit that
 * changed the text posts UIElementEvent_Committed once; a cancel (with EscapeReverts) puts back the text at entering instead, posting
 * UIElementEvent_Cancelled when the text had changed; every edit posts UIElementEvent_Changed. Its own context menu does not end a visit:
 * the menu takes the focus and gives it back, and the visit goes on (a menu closed by a press elsewhere ends it there). The events carry the text's length in
 * bytes as a double: the text itself is read by id (UITextBox_GetText), the box being alive at delivery unless something destroyed it in
 * that update. An edit made while the box is not entered (the short list's paste and reset, a menu item) is a whole change: Changed, then
 * Committed. Code setting the text posts nothing.
 *
 * THE MOUSE. A press places the caret and a drag selects (past an edge the text scrolls, faster further out); Shift with a press extends
 * the selection; a double click selects a word and a triple click a line, up to its line breaks, and dragging on after them selects whole
 * words or lines. A right press outside the selection moves the caret there first; a right click opens the box's context menu (Undo, Redo,
 * Cut, Copy, Paste, Delete, Select all, each disabled when it cannot act), through the context menu service, so calling code can give the
 * box a menu of its own instead. The wheel scrolls it (Shift: across) and goes up the tree at the end. The pointer shows an I-beam over it.
 *
 * NOT ENTERED (focused or hovered: the short list): copy puts the whole text on the clipboard (refused when masked), paste replaces the whole
 * text as one change and one undo step, reset goes back to the box's default when it has one.
 *
 * UNDO is the box's own (UITextEdit.h: grouped by time and count). While the box is entered the undo and redo keys are always its own, even
 * with nothing left (the Refused cue), so typing never undoes something behind it; not entered, they are not its. The list lives as long as
 * the box (UITextBoxUndoScope_Box), or starts empty at each visit (UITextBoxUndoScope_Visit: an editor's field, whose own history records
 * the visit's Committed). Text set by code (UITextBox_SetText) empties it; UITextBox_ReplaceRange is an edit like typing, undoable. A
 * filter or validator in the limits must not change the box: its edits are refused while they run.
 *
 * READ-ONLY (IsEditable false): no caret and no typing; selectable (IsSelectable), it is an edit element whose keys scroll it (the arrows,
 * the page keys, Ctrl+Home, Ctrl+End) and copy with Ctrl+A and Ctrl+C; not selectable, it takes no focus. Its look draws it with a box of its
 * own. It can show a whole text component tree (UITextBox_SetComponent), translate and sprite nodes included, with selection over the
 * flow's text (a sprite is one object character there, left out when copied); a box showing a tree changes by a new tree or a text, never
 * a range of it, and keeps it whole through new limits; an editable box flattens a tree to its text, without its sprites, and says what it
 * could not keep. MASKED (IsMasked): one mask character per character (the theme's; a tree's too), copy and cut refused, the whole text one
 * word.
 *
 * SCROLLING: the scrollable piece (UIScrollable.h) by the box's modes, with two scroll bars the kit makes with it (the scroll bar kind's look),
 * and the caret kept in view (trailing spaces count). Wrapped, lines break at the view's width and the box scrolls down, and across only to
 * a word longer than a line (words are not broken); not wrapped, it scrolls both ways.
 *
 * STYLE RANGES (UITextBox_SetStyleRanges): byte ranges of the text with what a TextStyle carries (a colour, a font, a size relative to the
 * box's text, a shadow, underline, strikethrough, spacing), set by code and moved with every edit, undo and redo. The user types plain text.
 *
 * LOOKS. A look (UIKit_RegisterLook: a struct starting with UITextBoxLookBase) lays the text out and draws it. It tells the behaviour how in
 * its Construct (UITextBoxCapability_SetGeometry: its text's insets, a layout of the text with carets, a line's height, the border its bars
 * stand inside), builds its
 * document from the box's content (UITextBoxCapability_BuildDocument, again whenever UITextBoxCapability_GetContentRevision moves or the
 * mask or the text colour it built with changes) and reads the rest to draw: the state, the view's box and the scroll offset, the selection
 * and the caret in layout offsets, whether the placeholder shows.
 */


// Forward declarations.
/** @brief A text component; full type in TextComponent.h. */
typedef struct TextComponentStruct TextComponent;


// Macros.
/** @brief The most style ranges a box holds. */
#define UI_TEXT_BOX_MAX_STYLE_RANGES ((size_t)4096)
/** @brief The most lines a box asks to be tall (UITextBoxOptions.VisibleLines). */
#define UI_TEXT_BOX_MAX_VISIBLE_LINES ((size_t)200)
/** @brief How far past an edge the drag's scroll speeds up, in UI pixels per second for each UI pixel the pointer is out, and its bounds. */
#define UI_TEXT_BOX_EDGE_SCROLL_RATE 12.0f
#define UI_TEXT_BOX_MIN_EDGE_SCROLL_SPEED 60.0f
#define UI_TEXT_BOX_MAX_EDGE_SCROLL_SPEED 4000.0f


// Types.
/**
 * @brief How long a box's undo list lives.
 */
typedef enum UITextBoxUndoScopeEnum
{
    /** @brief As long as the box, across visits: click away, come back, and undo still works. The default. */
    UITextBoxUndoScope_Box,
    /** @brief One visit: the list starts empty each time the box is entered, so each change has one undo path, the box's while typing and an
     *  editor's after. */
    UITextBoxUndoScope_Visit
} UITextBoxUndoScope;

/**
 * @brief What a text box is created with (UIKit_CreateTextBox). A plain value; build one with UITextBoxOptions_Create or
 *        UITextBoxOptions_CreateMultiline.
 */
typedef struct UITextBoxOptionsStruct
{
    /** @brief What every element is created with. */
    UIElementOptions Element;
    /** @brief The text it starts with, UTF-8 through the limits, or NULL for none. Read for the call. */
    const unsigned char* Text;
    /** @brief What shows while the text is empty, UTF-8, or NULL for nothing. Copied. */
    const unsigned char* Placeholder;
    /** @brief What the text may hold (UITextEdit.h). A one-line box allows no line breaks. */
    UITextEditLimits Limits;
    /** @brief How its undo groups and how much it keeps. */
    UITextEditUndoOptions Undo;
    /** @brief How long its undo list lives. */
    UITextBoxUndoScope UndoScope;
    /** @brief Whether the text can be typed into; read-only otherwise. */
    bool IsEditable;
    /** @brief Whether the text can be selected and copied (an editable box always can). */
    bool IsSelectable;
    /** @brief Whether it takes input. */
    bool IsEnabled;
    /** @brief Whether Tab types a tab, in place of leaving (Shift+Tab still leaves backwards). */
    bool AcceptsTab;
    /** @brief Whether Insert turns overwrite mode on. */
    bool AllowsOverwrite;
    /** @brief Whether each character shows as the theme's mask character, with copy and cut refused (a password). */
    bool IsMasked;
    /** @brief Whether Escape puts back the text at entering; otherwise it keeps the text, as Enter does. */
    bool EscapeReverts;
    /** @brief Whether lines break at the view's width (and the box scrolls down) or run on (and it scrolls across too). */
    bool IsWrapped;
    /** @brief Whether it has a text to go back to (the reset action). */
    bool HasDefault;
    /** @brief The text to go back to, when HasDefault; NULL for an empty one. Copied. */
    const unsigned char* Default;
    /** @brief How it scrolls across. */
    UIScrollMode Horizontal;
    /** @brief How it scrolls down. */
    UIScrollMode Vertical;
    /** @brief How many lines tall it asks to be (its preferred height); within [1, UI_TEXT_BOX_MAX_VISIBLE_LINES]. */
    size_t VisibleLines;
} UITextBoxOptions;

/**
 * @brief What a text box look's Construct gets (UIKit_CreateTextBox passes it): the kit and the options.
 */
typedef struct UITextBoxArgsStruct
{
    /** @brief The kit; borrowed. */
    UIKit* Kit;
    /** @brief The options; borrowed for the call. */
    const UITextBoxOptions* Options;
} UITextBoxArgs;

/**
 * @brief A byte range of a box's text and how it shows. A plain value.
 */
typedef struct UITextStyleRangeStruct
{
    /** @brief The range's first byte offset. */
    size_t Start;
    /** @brief One past its last; above Start. */
    size_t End;
    /** @brief What it changes: the properties a TextStyle carries that apply to text (its size multiplies the box's text size: 1 is
     *  that size). Its font name is copied when the ranges are set. */
    TextStyle Style;
} UITextStyleRange;

/**
 * @brief How a text box's look answers where its text is: the behaviour asks it to place the caret under the pointer, to move up and down a
 *        line, to keep the caret in view, to decide its bars. A look sets one in its Construct (UITextBoxCapability_SetGeometry); its slots
 *        get the widget as @c widget.
 */
typedef struct UITextBoxGeometryStruct
{
    /** @brief The room between the box's edges and its text, in the widget's own UI pixels. */
    UIInsets (*GetTextInsets)(const void* widget);
    /** @brief Lays the text out if anything changed (its content, the theme, the scale, the wrap), with carets
     *  (TextLayoutOptions.TracksOffsets), wrapped at @p wrapWidth UI pixels when @p isWrapped, and answers the layout (borrowed until the
     *  next call) and how many of the widget's UI pixels a layout unit is. */
    Error (*LayOut)(void* widget, bool isWrapped, float wrapWidth, const TextLayout** outLayout, float* outUIPixelsPerUnit);
    /** @brief One line's height, in the widget's own UI pixels. */
    float (*GetLineHeight)(const void* widget);
    /** @brief How far in from the box's edges its bars stand, in the widget's own UI pixels: the box's border, so the bars stay inside it. */
    UIInsets (*GetBarInsets)(const void* widget);
} UITextBoxGeometry;

/**
 * @brief A text box's capability: its text piece, its scrollable piece, its options and what the keyboard and the mouse are doing. No look;
 *        embedded by a look's widget type right after its Widget (UITextBoxLookBase). Its fields are read-only outside the box's module.
 */
typedef struct UITextBoxCapabilityStruct
{
    /** @brief The text, the caret, the selection, the limits and the undo list. */
    UITextEdit Edit;
    /** @brief The view into the text; calling code reaches it by UIScrollable_*. */
    UIScrollableCapability Scrollable;
    /** @brief The options it was made with or given since. */
    bool _isEditable;
    bool _isSelectable;
    bool _isEnabled;
    bool _isInvalid;
    bool _acceptsTab;
    bool _allowsOverwrite;
    bool _isMasked;
    bool _escapeReverts;
    bool _isWrapped;
    bool _hasDefault;
    UITextBoxUndoScope _undoScope;
    size_t _visibleLines;
    /** @brief The text the reset action goes back to. */
    UITextContent _default;
    /** @brief What shows while the text is empty. */
    UITextContent _placeholder;
    /** @brief Whether typing replaces the character after the caret; ends when the box is left. */
    bool _isOverwriting;
    /** @brief The text at entering (bytes), while a visit is on. */
    GenericBuffer _enteredText;
    /** @brief Whether a visit is on: the box was entered and not left yet. */
    bool _isVisiting;
    /** @brief Whether the box is opening its own context menu (the menu takes the focus as it opens). */
    bool _isOpeningMenu;
    /** @brief Whether the visit goes on through the box's own menu: left for it, and back when the menu gives the focus back. */
    bool _isVisitSuspended;
    /** @brief The box's own menu's root while the visit goes on through it. */
    uint64_t _menuId;
    /** @brief The style ranges (private element type), in the order set. */
    GenericBuffer _styleRanges;
    /** @brief A read-only box's component tree, owned (a copy), or NULL: then the text shows as it is. */
    TextComponent* _component;
    /** @brief The factory the box's components come from; borrowed (the kit's). */
    TextComponentFactory* _factory;
    /** @brief Moves with whatever shows: the text, the ranges, the tree, the mask. */
    uint64_t _contentRevision;
    /** @brief Moves with every edit and caret move: a look restarts the caret's blink. */
    uint64_t _caretRevision;
    /** @brief The mask character the look's document was last built with. */
    uint32_t _builtMask;
    /** @brief A left press is selecting. */
    bool _isDragging;
    /** @brief What a drag selects by: 1 characters, 2 words, 3 lines. */
    uint32_t _dragUnit;
    /** @brief The word or line the double or triple click selected first. */
    size_t _dragStart;
    size_t _dragEnd;
    /** @brief Where the drag's pointer was last, normalized in the widget's box. */
    Vector2 _dragPointer;
    /** @brief Whether Up and Down have a column to keep, and the column's x, in content UI pixels. */
    bool _hasColumn;
    float _column;
    /** @brief Whether the next layout scrolls the caret into view. */
    bool _isRevealPending;
    /** @brief Where the text shows in the widget's box, in its UI pixels, as last laid out. */
    Rectangle _viewBox;
    /** @brief The wrap width last laid out with, in UI pixels. */
    float _wrapWidth;
    /** @brief The scroll bars by axis (UIScrollAxis), made with it; WIDGET_ID_INVALID until then. */
    uint64_t _barIds[UIScrollAxis_Count];
    /** @brief Which bars showed at the last layout. */
    bool _isBarShown[UIScrollAxis_Count];
    /** @brief The look's answers; NULL until it sets them. Static storage. */
    const UITextBoxGeometry* _geometry;
    /** @brief The screen's time as the box last heard it, in seconds: the undo's grouping counts with it. */
    double _time;
    /** @brief Scratch: text being built or copied (bytes). */
    GenericBuffer _scratch;
} UITextBoxCapability;

/**
 * @brief How every text box look's widget struct starts: the widget, then the capability.
 */
typedef struct UITextBoxLookBaseStruct
{
    /** @brief The widget. */
    Widget Base;
    /** @brief The box's behaviour. */
    UITextBoxCapability TextBox;
} UITextBoxLookBase;


// Helpers.
/**
 * @brief Default options for one line: no line breaks, editable and selectable, enabled, overwrite allowed, Escape reverts, not wrapped,
 *        scrolling across with no bar, one line tall, no placeholder and no default, the default limits and undo.
 * @returns The options.
 */
UITextBoxOptions UITextBoxOptions_Create(void);

/**
 * @brief Default options for several lines: line breaks allowed, Escape keeps the text, wrapped, scrolling down with a bar while the text
 *        is taller than the view and across with no bar only to a word longer than a line, four lines tall; the rest as
 *        UITextBoxOptions_Create.
 * @returns The options.
 */
UITextBoxOptions UITextBoxOptions_CreateMultiline(void);


// Calling code: by widget id.
/**
 * @brief Replaces a box's text, through its limits; the caret goes to its end with nothing selected, the undo list is emptied (its steps no
 *        longer match the text) and the style ranges are cut to the new text. Posts no event. A visit goes on from it.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param text The text as UTF-8; NULL for none.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; ErrorCode_InvalidTextEncoding for text that
 *          is not valid UTF-8, which changes nothing.
 */
Error UITextBox_SetText(UIScreen* screen, uint64_t widgetId, const unsigned char* text);

/**
 * @brief Reads a box's text.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param outText [out] Receives the NUL-terminated UTF-8, valid until the text changes; must not be NULL.
 * @param outLength [out] Receives its length in bytes; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box.
 */
Error UITextBox_GetText(UIScreen* screen, uint64_t widgetId, const unsigned char** outText, size_t* outLength);

/**
 * @brief Replaces a range of a box's text, through its limits, as an edit like typing: a step of its own in the undo list, the style ranges
 *        moved with it, the caret after what went in. Posts no event; a visit that is on ends in a Committed as usual. What a console's
 *        completion does.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param start The range's first byte offset (moved back onto a codepoint boundary, clamped to the text).
 * @param end One past its last; at least @p start.
 * @param text What goes there, UTF-8; NULL for nothing.
 * @returns Success (also when a limit refused all of it); ErrorCode_InvalidOperation for a stale id, a widget that is no text box or a box
 *          showing a component tree (it changes by a new tree or a text); ErrorCode_IllegalArgument for @p end below @p start.
 */
Error UITextBox_ReplaceRange(UIScreen* screen, uint64_t widgetId, size_t start, size_t end, const unsigned char* text);

/**
 * @brief Reads a box's selection.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param outAnchor [out] Receives the anchor's byte offset; may be NULL.
 * @param outCaret [out] Receives the caret's byte offset; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box.
 */
Error UITextBox_GetSelection(UIScreen* screen, uint64_t widgetId, size_t* outAnchor, size_t* outCaret);

/**
 * @brief Selects from an anchor to a caret (each moved back onto a codepoint boundary and clamped); equal offsets only place the caret.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param anchor The anchor's byte offset.
 * @param caret The caret's byte offset.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box.
 */
Error UITextBox_SetSelection(UIScreen* screen, uint64_t widgetId, size_t anchor, size_t caret);

/**
 * @brief Changes a box's limits: the text is cut to fit them, and the undo list is emptied. A box showing a component tree keeps it and its
 *        text whole: the limits hold for a text set later. Posts no event.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param limits The limits; must not be NULL and valid (UITextEditLimits_IsValid). Copied.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; ErrorCode_IllegalArgument if @p limits is
 *          NULL; ErrorCode_ArgumentOutOfRange for limits that are not valid, which changes nothing.
 */
Error UITextBox_SetLimits(UIScreen* screen, uint64_t widgetId, const UITextEditLimits* limits);

/**
 * @brief Changes how a box's undo groups and how long its list lives; the list is emptied.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param options How it groups; must not be NULL and valid. Copied.
 * @param scope How long the list lives.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; ErrorCode_IllegalArgument if @p options is
 *          NULL; ErrorCode_ArgumentOutOfRange for options or a scope that are not valid, which changes nothing.
 */
Error UITextBox_SetUndoOptions(UIScreen* screen, uint64_t widgetId, const UITextEditUndoOptions* options, UITextBoxUndoScope scope);

/**
 * @brief Changes what shows while a box's text is empty.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param placeholder The text as UTF-8; NULL for nothing.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; ErrorCode_InvalidTextEncoding for text that
 *          is not valid UTF-8, which changes nothing.
 */
Error UITextBox_SetPlaceholder(UIScreen* screen, uint64_t widgetId, const unsigned char* placeholder);

/**
 * @brief Sets the text a box goes back to on the reset action.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param hasDefault Whether it has one; without one the reset action goes on to what lies behind it.
 * @param text The text as UTF-8, when @p hasDefault; NULL for an empty one.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; ErrorCode_InvalidTextEncoding for text that
 *          is not valid UTF-8, which changes nothing.
 */
Error UITextBox_SetDefault(UIScreen* screen, uint64_t widgetId, bool hasDefault, const unsigned char* text);

/**
 * @brief Makes a box writable or read-only. Made read-only while entered, it stays entered, its keys scrolling it. A box showing a component
 *        tree made writable keeps the tree's text without its sprites, through its limits, as UITextBox_SetComponent does for one.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param isEditable Whether the text can be typed into.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; otherwise a hook's error.
 */
Error UITextBox_SetIsEditable(UIScreen* screen, uint64_t widgetId, bool isEditable);

/**
 * @brief Sets whether a read-only box's text can be selected and copied; one that cannot takes no focus.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param isSelectable Whether it can.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; otherwise a hook's error.
 */
Error UITextBox_SetIsSelectable(UIScreen* screen, uint64_t widgetId, bool isSelectable);

/**
 * @brief Enables or disables a box. Disabled, it takes no focus (so it is left, keeping its text) and swallows presses.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param isEnabled Whether it takes input.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; otherwise a hook's error.
 */
Error UITextBox_SetIsEnabled(UIScreen* screen, uint64_t widgetId, bool isEnabled);

/**
 * @brief Marks a box's text as refused or not (a name that is taken, say): its look shows the invalid state.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param isInvalid Whether it is.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box.
 */
Error UITextBox_SetIsInvalid(UIScreen* screen, uint64_t widgetId, bool isInvalid);

/**
 * @brief Masks a box's text or shows it.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param isMasked Whether each character shows as the mask.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box.
 */
Error UITextBox_SetIsMasked(UIScreen* screen, uint64_t widgetId, bool isMasked);

/**
 * @brief Sets a box's keys' choices: whether Tab types a tab, whether Insert turns overwrite on, whether Escape puts the text back.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param acceptsTab Whether Tab types a tab.
 * @param allowsOverwrite Whether Insert turns overwrite mode on (off, it ends overwrite mode).
 * @param escapeReverts Whether Escape puts back the text at entering.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box.
 */
Error UITextBox_SetKeys(UIScreen* screen, uint64_t widgetId, bool acceptsTab, bool allowsOverwrite, bool escapeReverts);

/**
 * @brief Sets whether a box's lines break at its view's width.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param isWrapped Whether they do.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box.
 */
Error UITextBox_SetIsWrapped(UIScreen* screen, uint64_t widgetId, bool isWrapped);

/**
 * @brief Sets how a box's text shows, by byte ranges, in place of the ranges it had: later ranges show over earlier ones where they overlap.
 *        Edits move them as the text moves (an insertion inside a range grows it; one at its end does not), and so do undo and redo: they
 *        do not put back a range's end an edit cut (code that colours a text sets its ranges again after a change).
 *
 * The user types plain text: formatting a user chooses and that is stored (a paper editor's) is not here. A keystroke costs each piece of
 * the text the ranges over it: disjoint ranges little, deeply nested ones the square of their count.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param ranges The ranges; may be NULL only when @p count is 0. Each copied, its font name too; their ends are moved back onto codepoint
 *        boundaries and clamped to the text.
 * @param count How many; at most UI_TEXT_BOX_MAX_STYLE_RANGES.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; ErrorCode_IllegalArgument for NULL ranges with
 *          a count, or a range whose end is not above its start; ErrorCode_ArgumentOutOfRange for too many ranges or a font name that is no
 *          valid asset name; a string component's error for a style it would refuse (a size or a spacing out of its bounds), so no build
 *          ever fails on one. A failure changes nothing.
 */
Error UITextBox_SetStyleRanges(UIScreen* screen, uint64_t widgetId, const UITextStyleRange* ranges, size_t count);

/**
 * @brief Reads one of a box's style ranges, as the edits since they were set moved it.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param index The range's index; ranges an edit emptied are gone, so the count can fall.
 * @param outRange [out] Receives it; its style's font name points into the box (valid until the ranges change). Must not be NULL.
 * @param outCount [out] Receives how many ranges the box has; may be NULL.
 * @returns Success (with outRange left alone for an index past the count); ErrorCode_InvalidOperation for a stale id or a widget that is no
 *          text box.
 */
Error UITextBox_GetStyleRange(UIScreen* screen, uint64_t widgetId, size_t index, UITextStyleRange* outRange, size_t* outCount);

/**
 * @brief Shows a whole text component tree in a box. A read-only box keeps it as it is (translate and sprite nodes included) and its text is
 *        the tree's flow text (a sprite one U+FFFC, a translate node its fallback, else its key), over which selection works. An editable box
 *        flattens it to its text, through the limits, dropping its styles and its sprites. Either way the undo list is emptied and the style
 *        ranges cleared.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @param root The tree; must not be NULL. Copied; the caller keeps it.
 * @param outIsKeptWhole [out] Receives whether the box shows everything the tree had (false when an editable box dropped styles, sprites or
 *        translations, or a limit cut its text); may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; ErrorCode_IllegalArgument if @p root is NULL;
 *          otherwise the copy's error, which changes nothing.
 */
Error UITextBox_SetComponent(UIScreen* screen, uint64_t widgetId, const TextComponent* root, bool* outIsKeptWhole);


// Behaviour helpers, for looks and the kit.
/**
 * @brief Constructs the capability from the kind's arguments.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @param args The arguments; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for limits, undo options, modes or a line count that are not valid;
 *          ErrorCode_InvalidTextEncoding for text that is not valid UTF-8; otherwise the options' error. Nothing is left to release on
 *          failure.
 */
Error UITextBoxCapability_Construct(UITextBoxCapability* self, Widget* widget, const UITextBoxArgs* args);

/**
 * @brief Releases the capability.
 * @param self The capability; must not be NULL.
 * @returns Success, or the tree's release error.
 */
Error UITextBoxCapability_Deconstruct(UITextBoxCapability* self);

/**
 * @brief Gives the behaviour the look's answers: call it from the look's Construct.
 * @param self The capability; must not be NULL.
 * @param geometry The answers; static storage, every slot set. NULL takes them away (the box then places no caret by the pointer and moves
 *        only along its text).
 */
void UITextBoxCapability_SetGeometry(UITextBoxCapability* self, const UITextBoxGeometry* geometry);

/**
 * @brief Returns the state a look shows for the box: disabled, pressed (a drag), invalid, entered, hovered, focused or normal.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns The state.
 */
UIElementState UITextBoxCapability_GetState(const UITextBoxCapability* self, const Widget* widget);

/**
 * @brief Returns the revision of what shows, which moves with the text, the style ranges, the tree and masking: a look builds its document
 *        again when it moved (UITextBoxCapability_BuildDocument).
 * @param self The capability; must not be NULL.
 * @returns The revision.
 */
static inline uint64_t UITextBoxCapability_GetContentRevision(const UITextBoxCapability* self)
{
    return self->_contentRevision;
}

/**
 * @brief Returns the revision of the caret, which moves with every edit and caret move: a look restarts the caret's blink when it moved.
 * @param self The capability; must not be NULL.
 * @returns The revision.
 */
static inline uint64_t UITextBoxCapability_GetCaretRevision(const UITextBoxCapability* self)
{
    return self->_caretRevision;
}

/**
 * @brief Builds the component tree a look's document shows for the box: the read-only box's tree, or the text as string components cut at
 *        the style ranges (each of size 1, so a layout's multiplier gives the theme's size; a masked text as one mask character per
 *        character, line breaks kept), the plain parts in @p textColor.
 * @param self The capability; must not be NULL.
 * @param factory The factory the components come from; must not be NULL (the kit's: UIKit_GetTextFactory).
 * @param mask The mask character, a codepoint the theme gives (UITextBoxPart_Mask); used when the box is masked.
 * @param textColor The colour of the text no style range colours.
 * @param outRoot [out] Receives the tree's root, the caller's; must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a mask that is no codepoint; otherwise the components' error, with nothing left over.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UITextBoxCapability_BuildDocument(UITextBoxCapability* self, TextComponentFactory* factory, uint32_t mask, RenderColor textColor,
    TextComponent** outRoot);

/**
 * @brief Returns whether the box shows its placeholder: its text is empty and it has one.
 * @param self The capability; must not be NULL.
 * @returns true if it does.
 */
bool UITextBoxCapability_IsShowingPlaceholder(const UITextBoxCapability* self);

/**
 * @brief Returns the placeholder's text.
 * @param self The capability; must not be NULL.
 * @returns The content.
 */
static inline const UITextContent* UITextBoxCapability_GetPlaceholder(const UITextBoxCapability* self)
{
    return &self->_placeholder;
}

/**
 * @brief Returns whether the box shows a caret: it is editable, enabled and entered.
 * @param self The capability; must not be NULL.
 * @param widget The widget; must not be NULL.
 * @returns true if it does.
 */
bool UITextBoxCapability_IsCaretShown(const UITextBoxCapability* self, const Widget* widget);

/**
 * @brief Returns whether typing replaces the character after the caret: a look draws the caret as a block then.
 * @param self The capability; must not be NULL.
 * @returns true in overwrite mode.
 */
static inline bool UITextBoxCapability_IsOverwriting(const UITextBoxCapability* self)
{
    return self->_isOverwriting;
}

/**
 * @brief Returns whether the box is read-only: a look draws it with the read-only box part.
 * @param self The capability; must not be NULL.
 * @returns true when it cannot be typed into.
 */
static inline bool UITextBoxCapability_IsReadOnly(const UITextBoxCapability* self)
{
    return !self->_isEditable;
}

/**
 * @brief Returns how many lines tall the box asks to be: what a look's Measure counts.
 * @param self The capability; must not be NULL.
 * @returns The count.
 */
static inline size_t UITextBoxCapability_GetVisibleLines(const UITextBoxCapability* self)
{
    return self->_visibleLines;
}

/**
 * @brief Returns where the text shows in the widget's box, as last laid out.
 * @param self The capability; must not be NULL.
 * @returns The box, in the widget's own UI pixels.
 */
static inline Rectangle UITextBoxCapability_GetViewBox(const UITextBoxCapability* self)
{
    return self->_viewBox;
}

/**
 * @brief Returns how far the view is scrolled into the text, as last laid out (the offset the look draws with).
 * @param self The capability; must not be NULL.
 * @returns The offset in the widget's own UI pixels.
 */
static inline Vector2 UITextBoxCapability_GetScrollOffset(const UITextBoxCapability* self)
{
    return self->Scrollable._placedOffset;
}

/**
 * @brief Returns the caret's offset in the look's layout (a masked text's offsets count its mask characters).
 * @param self The capability; must not be NULL.
 * @returns The layout offset.
 */
size_t UITextBoxCapability_GetLayoutCaret(const UITextBoxCapability* self);

/**
 * @brief Returns the selection in the look's layout's offsets.
 * @param self The capability; must not be NULL.
 * @param outStart [out] Receives its first layout offset; must not be NULL.
 * @param outEnd [out] Receives one past its last; the start when nothing is selected. Must not be NULL.
 */
void UITextBoxCapability_GetLayoutSelection(const UITextBoxCapability* self, size_t* outStart, size_t* outEnd);

/**
 * @brief Measures a box's preferred size for its look's Measure: its lines (UITextBoxCapability_GetVisibleLines) of the look's line height
 *        and the insets, at least @p minimum, with the bars that always show added.
 * @param self The capability; must not be NULL.
 * @param widget The box's widget; must not be NULL.
 * @param context The measuring context; must not be NULL.
 * @param minimum The smallest size, in its own UI pixels.
 * @param outSize [out] Receives the size in its own UI pixels; must not be NULL.
 * @returns Success, or a bar's Measure error.
 */
Error UITextBox_MeasurePreferred(UITextBoxCapability* self, Widget* widget, const UIMeasureContext* context, Vector2 minimum, Vector2* outSize);

/**
 * @brief Records the box's scroll bars, made by the kit right after the box. Called by UIKit_CreateTextBox.
 * @param self The capability; must not be NULL.
 * @param horizontalBarId The bar across.
 * @param verticalBarId The bar down.
 */
void UITextBox_SetBars(UITextBoxCapability* self, uint64_t horizontalBarId, uint64_t verticalBarId);

/**
 * @brief Gives a box its context menu: what the kit does when it makes one. The menu (Undo, Redo, Cut, Copy, Paste, Delete, Select all)
 *        goes through the context menu service, so calling code may set a provider of its own in its place.
 * @param screen The box's screen; must not be NULL.
 * @param widgetId The box.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget that is no text box; otherwise the service's error.
 */
Error UITextBox_AttachContextMenu(UIScreen* screen, uint64_t widgetId);
