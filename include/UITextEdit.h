#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


/**
 * @file UITextEdit.h
 * @brief The text piece: UTF-8 text with a caret and a selection, limits on what it may hold, word boundaries and a grouped undo, as
 *        pure data and functions, for every element that edits text (the text box; later the property box).
 *
 * STATE. The text (valid UTF-8, never more than its limits allow), the caret and the selection's anchor as byte offsets on codepoint
 * boundaries (the selection is what lies between them), and the undo list. No widget, no font and no layout: where a caret shows and
 * what is above it are the element's, through its text layout. An edit is a function on the state, its limits and the time it
 * happens at, so tests drive it with times of their own.
 *
 * WHAT COMES IN goes through one function, whether typed, pasted, set by code or an edit from code: invalid UTF-8 becomes U+FFFD, a
 * "\r\n" or a lone '\r' becomes '\n', control characters other than '\n' and '\t' are dropped, a '\n' the limits do not allow (no line
 * breaks, or no lines left) becomes a space, forbidden codepoints and those the filter refuses are dropped, and what does not fit the
 * byte, character and line budgets is cut at a codepoint. The limits judge the result: what an edit replaces does not count, so typing
 * over a selection in a full text works, and overwriting a character is no growth. A validator, when set, then accepts or refuses the
 * whole result. An edit that lost anything this way reports it refused (UITextEditChange.IsRefused). Typing a line break where none may
 * go is refused whole (UITextEdit_TypeCodepoint: Enter at the line limit types nothing); text that comes in otherwise turns it into a
 * space. The filter and the validator must not change the text piece (nor what holds it): while they run its edits are refused with
 * ErrorCode_InvalidOperation, so the edit they judge stays whole.
 *
 * WORDS (Ctrl with the arrows, Ctrl with the deletions, a double click): a run of letters, digits and underscores is a word, a run of
 * other symbols is a word of its own, and spaces and line breaks are skipped. Letters and digits are told apart from symbols by a small
 * table (UITextEdit_GetCharacterClass): ASCII by its classes, and outside it the punctuation, symbol and space blocks; every other
 * codepoint counts as a letter, which is right for the scripts the game shows. A masked text is one word, so word movement reveals
 * nothing of it.
 *
 * UNDO. Every edit is a step: the byte range it replaced, the bytes removed and inserted, and the caret and the anchor before and after.
 * Undo puts the text and the selection back as they were before the step; redo puts the text back and the caret after it; a new edit
 * clears what could be redone. Consecutive edits of one kind (typing, overwriting, deleting backward, deleting forward) merge into one
 * step until one of these happens: a pause longer than UITextEditUndoOptions.PauseSeconds between two edits, the step holding
 * UITextEditUndoOptions.GroupCharacters characters, the caret or the selection moving between two edits (any caret function, or
 * UITextEdit_CloseUndoGroup), another kind of edit, an undo or a redo. Typing over a selection starts a step the typing after it joins.
 * A UITextEditKind_Whole edit (a paste, a cut, a deleted word, an edit from code) is always a step of its own. The steps' records and
 * bytes stay within a budget (UITextEditUndoOptions.BudgetBytes) and at most UI_TEXT_EDIT_MAX_UNDO_STEPS: the oldest go first, and a
 * step larger than the whole budget empties the list, so that edit cannot be undone; the edit itself is never refused for it. Undo and
 * redo restore what was there, so no limit refuses them. Text set by code (UITextEdit_SetText) and a change of the limits empty the
 * list. A mark (UITextEdit_SetMark) remembers where the list stood, so an element can go back to it (UITextEdit_RevertToMark): what a
 * cancel does, the steps undone staying redoable.
 *
 * Construct with UITextEdit_Construct and release with UITextEdit_Deconstruct. It allocates as the text and the undo list grow, never
 * more than the limits and the budget allow, and reuses its buffers. Not thread-safe.
 */


// Macros.
/** @brief The hard ceiling of UITextEditLimits.MaxBytes: a keystroke lays the whole text out again, so a text past this needs a layout
 *  that rebuilds only what changed (a script editor's). */
#define UI_TEXT_EDIT_MAX_BYTES ((size_t)(1024U * 1024U))
/** @brief The byte limit a text has unless it says otherwise (UITextEditLimits_Create). */
#define UI_TEXT_EDIT_DEFAULT_MAX_BYTES ((size_t)(64U * 1024U))
/** @brief The most forbidden codepoints a limits value lists. */
#define UI_TEXT_EDIT_MAX_FORBIDDEN ((size_t)32)
/** @brief The most undo steps a text keeps. */
#define UI_TEXT_EDIT_MAX_UNDO_STEPS ((size_t)1000)
/** @brief The undo budget unless a text says otherwise, in bytes. */
#define UI_TEXT_EDIT_DEFAULT_UNDO_BUDGET ((size_t)(256U * 1024U))
/** @brief The largest undo budget, in bytes. */
#define UI_TEXT_EDIT_MAX_UNDO_BUDGET ((size_t)(16U * 1024U * 1024U))
/** @brief The pause that ends a group of typing unless a text says otherwise, in seconds. */
#define UI_TEXT_EDIT_DEFAULT_UNDO_PAUSE_SECONDS 1.0
/** @brief The longest pause a text may give, in seconds. */
#define UI_TEXT_EDIT_MAX_UNDO_PAUSE_SECONDS 60.0
/** @brief The most characters a group of typing holds unless a text says otherwise. */
#define UI_TEXT_EDIT_DEFAULT_UNDO_GROUP_CHARACTERS ((size_t)20)
/** @brief The codepoint invalid UTF-8 becomes. */
#define UI_TEXT_EDIT_REPLACEMENT_CODEPOINT ((uint32_t)0xFFFDU)


// Types.
/**
 * @brief Decides whether a codepoint may go into a text: what a box for numbers or names keeps out.
 * @param codepoint The codepoint: never a control character but '\n' and '\t' (the others are dropped before).
 * @param userData UITextEditLimits.FilterData.
 * @returns true to keep it; false drops it, and the edit reports itself refused.
 * @note It must not change the text piece: its edits are refused while it runs.
 */
typedef bool (*UITextEditFilter)(uint32_t codepoint, const UserData* userData);

/**
 * @brief Decides whether a whole result may stand, after every other limit: anything a codepoint at a time cannot judge.
 * @param text The text the edit would leave, valid UTF-8, NUL-terminated.
 * @param length Its length in bytes.
 * @param userData UITextEditLimits.FilterData.
 * @returns true to accept it; false refuses the edit, which changes nothing.
 * @note It must not change the text piece: its edits are refused while it runs.
 */
typedef bool (*UITextEditValidator)(const unsigned char* text, size_t length, const UserData* userData);

/**
 * @brief What a text may hold. A plain value; build one with UITextEditLimits_Create and change what you need.
 */
typedef struct UITextEditLimitsStruct
{
    /** @brief The most bytes; within [0, UI_TEXT_EDIT_MAX_BYTES]. */
    size_t MaxBytes;
    /** @brief Whether MaxCharacters counts. */
    bool HasMaxCharacters;
    /** @brief The most codepoints, when HasMaxCharacters. */
    size_t MaxCharacters;
    /** @brief Whether MaxLines counts. */
    bool HasMaxLines;
    /** @brief The most lines (line breaks plus one), when HasMaxLines; at least 1. */
    size_t MaxLines;
    /** @brief Whether '\n' may go in at all; without it every line break that comes in becomes a space. */
    bool AllowsNewlines;
    /** @brief Codepoints that never go in; the first ForbiddenCount count. */
    uint32_t ForbiddenCodepoints[UI_TEXT_EDIT_MAX_FORBIDDEN];
    /** @brief How many of ForbiddenCodepoints count; at most UI_TEXT_EDIT_MAX_FORBIDDEN. */
    size_t ForbiddenCount;
    /** @brief Decides per codepoint, after the forbidden ones; may be NULL. */
    UITextEditFilter Filter;
    /** @brief Decides on the whole result; may be NULL. */
    UITextEditValidator Validator;
    /** @brief Handed to Filter and Validator. */
    UserData FilterData;
} UITextEditLimits;

/**
 * @brief How a text's undo groups and how much it keeps. A plain value; build one with UITextEditUndoOptions_Create.
 */
typedef struct UITextEditUndoOptionsStruct
{
    /** @brief How much memory the steps' records and bytes may take, in bytes; within [1, UI_TEXT_EDIT_MAX_UNDO_BUDGET]. */
    size_t BudgetBytes;
    /** @brief A pause longer than this between two edits ends a group, in seconds; within [0, UI_TEXT_EDIT_MAX_UNDO_PAUSE_SECONDS]. */
    double PauseSeconds;
    /** @brief The most characters a group holds; at least 1. */
    size_t GroupCharacters;
} UITextEditUndoOptions;

/**
 * @brief What kind of edit one is, for grouping undo steps.
 */
typedef enum UITextEditKindEnum
{
    /** @brief Typed characters inserted (and a selection they replace). */
    UITextEditKind_Typing,
    /** @brief Typed characters each replacing the one after the caret. */
    UITextEditKind_Overwriting,
    /** @brief Characters deleted before the caret, one at a time. */
    UITextEditKind_DeletingBackward,
    /** @brief Characters deleted after the caret, one at a time. */
    UITextEditKind_DeletingForward,
    /** @brief A step of its own: a paste, a cut, a deleted word, a whole change, an edit from code. */
    UITextEditKind_Whole
} UITextEditKind;

/**
 * @brief What a character is to word movement (UITextEdit_GetCharacterClass).
 */
typedef enum UITextEditCharacterClassEnum
{
    /** @brief A letter, a digit or an underscore: part of a word. */
    UITextEditCharacterClass_Word,
    /** @brief Another symbol: a run of them is a word of its own. */
    UITextEditCharacterClass_Symbol,
    /** @brief A space or a tab: skipped. */
    UITextEditCharacterClass_Space,
    /** @brief A line break: skipped, and never part of a word or a run of spaces. */
    UITextEditCharacterClass_LineBreak
} UITextEditCharacterClass;

/**
 * @brief What an edit, an undo or a redo did to the text. A plain value.
 */
typedef struct UITextEditChangeStruct
{
    /** @brief Whether the text changed. */
    bool HasChanged;
    /** @brief Whether something was refused: some or all of what came in was cut or dropped by a limit, or the validator said no, or
     *  there was nothing to undo or redo. */
    bool IsRefused;
    /** @brief Where the replaced bytes started, when HasChanged. */
    size_t Start;
    /** @brief How many bytes were removed there. */
    size_t RemovedLength;
    /** @brief How many bytes were inserted there. */
    size_t InsertedLength;
} UITextEditChange;

/**
 * @brief Hears each change UITextEdit_RevertToMark makes, as it makes it: what moves style ranges along with the text.
 * @param change The change: an undone or redone step, or the whole text set.
 * @param userData What the caller handed to UITextEdit_RevertToMark.
 */
typedef void (*UITextEditObserver)(const UITextEditChange* change, const UserData* userData);

/**
 * @brief The text piece's state. All fields are read-only outside the module; use the functions.
 */
typedef struct UITextEditStruct
{
    /** @brief The text and a counted NUL after it (bytes). */
    GenericBuffer _text;
    /** @brief How many codepoints the text holds. */
    size_t _characterCount;
    /** @brief How many '\n' it holds. */
    size_t _newlineCount;
    /** @brief The caret's byte offset, on a codepoint boundary. */
    size_t _caret;
    /** @brief The selection's other end, on a codepoint boundary; the caret's offset when nothing is selected. */
    size_t _anchor;
    /** @brief What the text may hold. */
    UITextEditLimits _limits;
    /** @brief How its undo groups and how much it keeps. */
    UITextEditUndoOptions _undoOptions;
    /** @brief The undo steps, oldest first (private element type). */
    GenericBuffer _steps;
    /** @brief Each step's removed bytes then its inserted bytes, in the steps' order (bytes). */
    GenericBuffer _undoBytes;
    /** @brief How many of the steps can be undone; the ones after them can be redone. */
    size_t _undoCount;
    /** @brief Whether the last step may take the next edit of its kind. */
    bool _isGroupOpen;
    /** @brief Whether _mark means anything. */
    bool _hasMark;
    /** @brief The undo count a mark remembered. */
    size_t _mark;
    /** @brief Scratch: what comes in, after the limits (bytes). */
    GenericBuffer _incoming;
    /** @brief Scratch: the result a validator judges (bytes). */
    GenericBuffer _candidate;
    /** @brief Whether the filter or the validator is running: the piece takes no edit then. */
    bool _isInCallback;
} UITextEdit;


// Helpers.
/**
 * @brief Default limits: UI_TEXT_EDIT_DEFAULT_MAX_BYTES, no character or line limit, line breaks allowed, nothing forbidden, no filter
 *        and no validator.
 * @returns The limits.
 */
UITextEditLimits UITextEditLimits_Create(void);

/**
 * @brief Returns whether limits make sense: MaxBytes within its ceiling, MaxLines at least 1 when it counts, ForbiddenCount within the
 *        array, and every forbidden codepoint a valid one.
 * @param limits The limits; must not be NULL.
 * @returns true if they do.
 */
bool UITextEditLimits_IsValid(const UITextEditLimits* limits);

/**
 * @brief Default undo options: UI_TEXT_EDIT_DEFAULT_UNDO_BUDGET, UI_TEXT_EDIT_DEFAULT_UNDO_PAUSE_SECONDS,
 *        UI_TEXT_EDIT_DEFAULT_UNDO_GROUP_CHARACTERS.
 * @returns The options.
 */
UITextEditUndoOptions UITextEditUndoOptions_Create(void);

/**
 * @brief Returns whether undo options make sense (UITextEditUndoOptions's bounds).
 * @param options The options; must not be NULL.
 * @returns true if they do.
 */
bool UITextEditUndoOptions_IsValid(const UITextEditUndoOptions* options);


// Lifecycle.
/**
 * @brief Starts an empty text with its limits and undo options.
 * @param self The text; must not be NULL.
 * @param limits What it may hold; must not be NULL and valid (UITextEditLimits_IsValid). Copied.
 * @param undoOptions How its undo groups; must not be NULL and valid. Copied.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_ArgumentOutOfRange for limits or options that are not
 *          valid. Nothing is left to release on failure.
 */
Error UITextEdit_Construct(UITextEdit* self, const UITextEditLimits* limits, const UITextEditUndoOptions* undoOptions);

/**
 * @brief Releases a text. Safe on NULL.
 * @param self The text, or NULL.
 */
void UITextEdit_Deconstruct(UITextEdit* self);


// Reading.
/**
 * @brief Returns the text.
 * @param self The text; must not be NULL.
 * @returns The NUL-terminated UTF-8; valid until it changes.
 */
static inline const unsigned char* UITextEdit_GetText(const UITextEdit* self)
{
    return (const unsigned char*)self->_text._data;
}

/**
 * @brief Returns the text's length.
 * @param self The text; must not be NULL.
 * @returns The length in bytes, its terminator not counted.
 */
static inline size_t UITextEdit_GetLength(const UITextEdit* self)
{
    return self->_text._count - 1U;
}

/**
 * @brief Returns how many codepoints the text holds.
 * @param self The text; must not be NULL.
 * @returns The count.
 */
static inline size_t UITextEdit_GetCharacterCount(const UITextEdit* self)
{
    return self->_characterCount;
}

/**
 * @brief Returns how many lines the text has: its line breaks plus one.
 * @param self The text; must not be NULL.
 * @returns The count.
 */
static inline size_t UITextEdit_GetLineCount(const UITextEdit* self)
{
    return self->_newlineCount + 1U;
}

/**
 * @brief Returns the caret's byte offset.
 * @param self The text; must not be NULL.
 * @returns The offset, on a codepoint boundary.
 */
static inline size_t UITextEdit_GetCaret(const UITextEdit* self)
{
    return self->_caret;
}

/**
 * @brief Returns the selection's anchor: its other end.
 * @param self The text; must not be NULL.
 * @returns The offset; the caret's when nothing is selected.
 */
static inline size_t UITextEdit_GetAnchor(const UITextEdit* self)
{
    return self->_anchor;
}

/**
 * @brief Returns whether anything is selected.
 * @param self The text; must not be NULL.
 * @returns true when the anchor and the caret differ.
 */
static inline bool UITextEdit_HasSelection(const UITextEdit* self)
{
    return self->_caret != self->_anchor;
}

/**
 * @brief Returns the selection as a range.
 * @param self The text; must not be NULL.
 * @param outStart [out] Receives its first byte offset; must not be NULL.
 * @param outEnd [out] Receives one past its last; equals the start when nothing is selected. Must not be NULL.
 */
void UITextEdit_GetSelection(const UITextEdit* self, size_t* outStart, size_t* outEnd);

/**
 * @brief Returns the text's limits.
 * @param self The text; must not be NULL.
 * @returns The limits; valid until they change.
 */
static inline const UITextEditLimits* UITextEdit_GetLimits(const UITextEdit* self)
{
    return &self->_limits;
}


// Code.
/**
 * @brief Replaces the whole text, through the limits (UTF-8 that is not valid becomes U+FFFD, as anything that comes in), puts the caret
 *        at its end with nothing selected and empties the undo list: its steps no longer match the text.
 * @param self The text; must not be NULL.
 * @param text The new text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param outChange [out] Receives what happened; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL text with a length.
 */
Error UITextEdit_SetText(UITextEdit* self, const unsigned char* text, size_t length, UITextEditChange* outChange);

/**
 * @brief Replaces the whole text with valid UTF-8 as it is, past the limits and the rules for what comes in, puts the caret at its start
 *        with nothing selected and empties the undo list: for a read-only view of a text whose byte offsets must match something else's (a
 *        text component tree's flow, as its layout counts it). An edit afterwards goes through the limits as usual.
 * @param self The text; must not be NULL.
 * @param text The new text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @returns Success; ErrorCode_IllegalArgument for a NULL text with a length; ErrorCode_InvalidTextEncoding for bytes that are no valid
 *          UTF-8 or hold a NUL, which changes nothing.
 */
Error UITextEdit_SetTextVerbatim(UITextEdit* self, const unsigned char* text, size_t length);

/**
 * @brief Changes the limits: the text is cut to fit them as text that comes in is (the caret and the anchor kept where they can be),
 *        and the undo list is emptied.
 * @param self The text; must not be NULL.
 * @param limits The limits; must not be NULL. Copied.
 * @param outChange [out] Receives what happened to the text; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p limits is NULL; ErrorCode_ArgumentOutOfRange for limits that are not valid, which
 *          changes nothing.
 */
Error UITextEdit_SetLimits(UITextEdit* self, const UITextEditLimits* limits, UITextEditChange* outChange);

/**
 * @brief Changes the undo options and empties the undo list.
 * @param self The text; must not be NULL.
 * @param options The options; must not be NULL. Copied.
 * @returns Success; ErrorCode_IllegalArgument if @p options is NULL; ErrorCode_ArgumentOutOfRange for options that are not valid, which
 *          changes nothing.
 */
Error UITextEdit_SetUndoOptions(UITextEdit* self, const UITextEditUndoOptions* options);


// Edits.
/**
 * @brief Replaces a range of the text with new text, through the limits, as an edit of a kind: what typing, pasting, cutting and an edit
 *        from code come down to. The range's ends are moved back onto codepoint boundaries and clamped to the text. The caret goes after
 *        what was inserted, with nothing selected, and the edit becomes an undo step (or joins the last one, by the grouping rules).
 * @param self The text; must not be NULL.
 * @param start The range's first byte offset.
 * @param end One past its last; at least @p start.
 * @param text What goes there; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param kind What kind of edit it is.
 * @param time When it happens, in seconds, for grouping.
 * @param outChange [out] Receives what happened; may be NULL.
 * @returns Success (also when a limit refused all of it: then nothing changed and IsRefused says so); ErrorCode_IllegalArgument for a
 *          NULL text with a length, or @p end below @p start; ErrorCode_ArgumentOutOfRange for an unknown kind.
 */
Error UITextEdit_Replace(UITextEdit* self, size_t start, size_t end, const unsigned char* text, size_t length, UITextEditKind kind,
    double time, UITextEditChange* outChange);

/**
 * @brief Replaces the selection (or inserts at the caret) with new text, through the limits: UITextEdit_Replace over the selection.
 * @param self The text; must not be NULL.
 * @param text What goes there; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param kind What kind of edit it is.
 * @param time When it happens, in seconds.
 * @param outChange [out] Receives what happened; may be NULL.
 * @returns As UITextEdit_Replace.
 */
Error UITextEdit_ReplaceSelection(UITextEdit* self, const unsigned char* text, size_t length, UITextEditKind kind, double time,
    UITextEditChange* outChange);

/**
 * @brief Types one codepoint: it replaces the selection; else, overwriting, the character after the caret (never a line break, and
 *        nothing at the text's end: there it goes in); else it goes in at the caret.
 * @param self The text; must not be NULL.
 * @param codepoint The codepoint.
 * @param isOverwriting Whether the text is in overwrite mode.
 * @param time When it happens, in seconds.
 * @param outChange [out] Receives what happened; may be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a value that is no codepoint.
 */
Error UITextEdit_TypeCodepoint(UITextEdit* self, uint32_t codepoint, bool isOverwriting, double time, UITextEditChange* outChange);

/**
 * @brief Deletes before the caret: the selection when there is one, else the character before the caret, or with @p isByWord everything
 *        back to the start of the word (UITextEdit_FindWordStart), a step of its own.
 * @param self The text; must not be NULL.
 * @param isByWord Whether to delete to the word's start.
 * @param isMasked Whether the text is masked (its words are the whole text).
 * @param time When it happens, in seconds.
 * @param outChange [out] Receives what happened; may be NULL.
 * @returns Success.
 */
Error UITextEdit_DeleteBackward(UITextEdit* self, bool isByWord, bool isMasked, double time, UITextEditChange* outChange);

/**
 * @brief Deletes after the caret: the selection when there is one, else the character after the caret, or with @p isByWord everything
 *        up to the end of the word (UITextEdit_FindWordEnd), a step of its own.
 * @param self The text; must not be NULL.
 * @param isByWord Whether to delete to the word's end.
 * @param isMasked Whether the text is masked.
 * @param time When it happens, in seconds.
 * @param outChange [out] Receives what happened; may be NULL.
 * @returns Success.
 */
Error UITextEdit_DeleteForward(UITextEdit* self, bool isByWord, bool isMasked, double time, UITextEditChange* outChange);


// The caret and the selection.
/**
 * @brief Moves the caret, moved back onto a codepoint boundary and clamped to the text; with @p isExtending the anchor stays (the
 *        selection grows or shrinks), else it follows (nothing selected). Ends the undo group.
 * @param self The text; must not be NULL.
 * @param offset The byte offset.
 * @param isExtending Whether the selection extends to it.
 */
void UITextEdit_SetCaret(UITextEdit* self, size_t offset, bool isExtending);

/**
 * @brief Selects from an anchor to a caret (each moved back onto a codepoint boundary and clamped). Ends the undo group.
 * @param self The text; must not be NULL.
 * @param anchor The anchor's byte offset.
 * @param caret The caret's byte offset.
 */
void UITextEdit_Select(UITextEdit* self, size_t anchor, size_t caret);

/**
 * @brief Selects the whole text, the caret at its end. Ends the undo group.
 * @param self The text; must not be NULL.
 */
void UITextEdit_SelectAll(UITextEdit* self);

/**
 * @brief What Left does: with a selection and no @p isExtending, the caret goes to the selection's start (and from there by a word with
 *        @p isByWord); else back a character, or to the start of the word.
 * @param self The text; must not be NULL.
 * @param isByWord Whether it moves by a word (Ctrl).
 * @param isExtending Whether the selection extends (Shift).
 * @param isMasked Whether the text is masked (by a word: to its start).
 */
void UITextEdit_MoveLeft(UITextEdit* self, bool isByWord, bool isExtending, bool isMasked);

/**
 * @brief What Right does: with a selection and no @p isExtending, the caret goes to the selection's end (and from there by a word with
 *        @p isByWord); else on a character, or to the end of the word.
 * @param self The text; must not be NULL.
 * @param isByWord Whether it moves by a word (Ctrl).
 * @param isExtending Whether the selection extends (Shift).
 * @param isMasked Whether the text is masked (by a word: to its end).
 */
void UITextEdit_MoveRight(UITextEdit* self, bool isByWord, bool isExtending, bool isMasked);


// Undo.
/**
 * @brief Returns whether a step can be undone.
 * @param self The text; must not be NULL.
 * @returns true if one can.
 */
static inline bool UITextEdit_CanUndo(const UITextEdit* self)
{
    return self->_undoCount > 0U;
}

/**
 * @brief Returns whether a step can be redone.
 * @param self The text; must not be NULL.
 * @returns true if one can.
 */
static inline bool UITextEdit_CanRedo(const UITextEdit* self)
{
    return self->_undoCount < self->_steps._count;
}

/**
 * @brief Returns how many steps the list holds, the redoable ones included.
 * @param self The text; must not be NULL.
 * @returns The count.
 */
static inline size_t UITextEdit_GetStepCount(const UITextEdit* self)
{
    return self->_steps._count;
}

/**
 * @brief Returns the most bytes of text coming in that an edit looks at: what lies past them is cut by any limit (a paste reads no more of
 *        the clipboard). A few bytes per byte of MaxBytes, for the invalid bytes and the line endings that shrink as they come in.
 * @param self The text piece; must not be NULL.
 * @returns The byte count.
 */
size_t UITextEdit_GetMostIncomingBytes(const UITextEdit* self);

/**
 * @brief Returns how much of its budget the undo list takes: its steps' records and bytes.
 * @param self The text; must not be NULL.
 * @returns The bytes.
 */
size_t UITextEdit_GetUndoMemory(const UITextEdit* self);

/**
 * @brief Undoes the last step: the text and the selection as they were before it. With nothing to undo, nothing changes and the change
 *        reports itself refused.
 * @param self The text; must not be NULL.
 * @param outChange [out] Receives what happened; may be NULL.
 * @returns Success.
 */
Error UITextEdit_Undo(UITextEdit* self, UITextEditChange* outChange);

/**
 * @brief Redoes the step after the last undone one: the text, and the caret after it. With nothing to redo, nothing changes and the change
 *        reports itself refused.
 * @param self The text; must not be NULL.
 * @param outChange [out] Receives what happened; may be NULL.
 * @returns Success.
 */
Error UITextEdit_Redo(UITextEdit* self, UITextEditChange* outChange);

/**
 * @brief Ends the undo group: the next edit is a step of its own. What a caret move by the mouse, entering and leaving do.
 * @param self The text; must not be NULL.
 */
static inline void UITextEdit_CloseUndoGroup(UITextEdit* self)
{
    self->_isGroupOpen = false;
}

/**
 * @brief Empties the undo list.
 * @param self The text; must not be NULL.
 */
void UITextEdit_ClearUndo(UITextEdit* self);

/**
 * @brief Remembers where the undo list stands, for UITextEdit_RevertToMark: what entering an element does.
 * @param self The text; must not be NULL.
 */
void UITextEdit_SetMark(UITextEdit* self);

/**
 * @brief Returns whether a mark is set and the list can still reach it: none of the steps it needs was dropped or cleared.
 * @param self The text; must not be NULL.
 * @returns true if it can.
 */
static inline bool UITextEdit_HasMark(const UITextEdit* self)
{
    return self->_hasMark;
}

/**
 * @brief Goes back to the text the mark saw: by undoing (or redoing) the steps since, which stay redoable, while the list still holds
 *        them; else, or when that does not give @p fallback, by setting @p fallback as the text (which empties the list). The caret and
 *        the selection come back with the steps, or go to the end. What a cancel does.
 * @param self The text; must not be NULL.
 * @param fallback The text at the mark; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param observer Hears each change as it is made; may be NULL.
 * @param userData Handed to @p observer; may be NULL.
 * @param outHasChanged [out] Receives whether the text changed; may be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL fallback with a length.
 */
Error UITextEdit_RevertToMark(UITextEdit* self, const unsigned char* fallback, size_t length, UITextEditObserver observer,
    const UserData* userData, bool* outHasChanged);


// Text helpers: pure functions on any valid UTF-8.
/**
 * @brief Returns what a codepoint is to word movement (see the file comment).
 * @param codepoint The codepoint.
 * @returns Its class.
 */
UITextEditCharacterClass UITextEdit_GetCharacterClass(uint32_t codepoint);

/**
 * @brief Moves an offset back onto the start of the codepoint it falls in, clamped to the text.
 * @param text The text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param offset The offset.
 * @returns The offset of a codepoint boundary.
 */
size_t UITextEdit_SnapToBoundary(const unsigned char* text, size_t length, size_t offset);

/**
 * @brief Returns the start of the codepoint before an offset.
 * @param text The text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param offset A codepoint boundary.
 * @returns The previous boundary; 0 at the start.
 */
size_t UITextEdit_GetPreviousBoundary(const unsigned char* text, size_t length, size_t offset);

/**
 * @brief Returns the boundary after the codepoint at an offset.
 * @param text The text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param offset A codepoint boundary.
 * @returns The next boundary; @p length at the end.
 */
size_t UITextEdit_GetNextBoundary(const unsigned char* text, size_t length, size_t offset);

/**
 * @brief Finds where Ctrl+Left goes from an offset: back over spaces and line breaks, then over the run of word characters or of
 *        symbols before it.
 * @param text The text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param offset A codepoint boundary.
 * @param isMasked Whether the text is masked: then 0.
 * @returns The word's start.
 */
size_t UITextEdit_FindWordStart(const unsigned char* text, size_t length, size_t offset, bool isMasked);

/**
 * @brief Finds where Ctrl+Right goes from an offset: on over spaces and line breaks, then over the run of word characters or of symbols
 *        after them.
 * @param text The text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param offset A codepoint boundary.
 * @param isMasked Whether the text is masked: then @p length.
 * @returns The word's end.
 */
size_t UITextEdit_FindWordEnd(const unsigned char* text, size_t length, size_t offset, bool isMasked);

/**
 * @brief Finds the word a double click at an offset selects: the run of the class of the character at the offset (or, at a run's end, of
 *        the one before it), a run of spaces being one too; nothing at a line break. A masked text is one word.
 * @param text The text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param offset A codepoint boundary.
 * @param isMasked Whether the text is masked.
 * @param outStart [out] Receives the word's start; must not be NULL.
 * @param outEnd [out] Receives its end; must not be NULL.
 */
void UITextEdit_GetWordAt(const unsigned char* text, size_t length, size_t offset, bool isMasked, size_t* outStart, size_t* outEnd);

/**
 * @brief Finds the line a triple click at an offset selects: from after the line break before it to the line break after it (not
 *        included), the whole text when it has none.
 * @param text The text; may be NULL only when @p length is 0.
 * @param length Its length in bytes.
 * @param offset The offset.
 * @param outStart [out] Receives the line's start; must not be NULL.
 * @param outEnd [out] Receives its end; must not be NULL.
 */
void UITextEdit_GetLineAt(const unsigned char* text, size_t length, size_t offset, size_t* outStart, size_t* outEnd);
