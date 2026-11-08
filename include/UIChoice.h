#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
// GenericBuffer is what option texts are written into, UserData what the providers are handed.
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


/**
 * @file UIChoice.h
 * @brief The choice piece: a list of options, the selected one (or none), and whether it is mixed (several things differ).
 *
 * Options are a list of texts (copied, in one block) or, for long ones, two providers asked when needed: how many there are, and the
 * text at an index; a dropdown's list asks only for the rows it shows, so 100 000 options cost what the rows in view cost, and keeps
 * the texts of the rows in view until it is told they changed (UIChoice_Refresh) or their count changes. Calling code talks to it by
 * widget id (UIChoice_SetOptions, _SetProvider, _Refresh, _SetSelected, _GetSelected, _SetMixed). Choosing an option is the element's:
 * it posts UIElementEvent_Changed and then UIElementEvent_Committed with the index as a double, unless the selection ended where it
 * began (choosing an option of a mixed choice always posts). Code setting the selection posts nothing.
 *
 * The capability struct and its helpers are for the elements that hold it.
 */


// Forward declarations.
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;
/** @brief The widget base; full type in UIWidget.h. */
typedef struct WidgetStruct Widget;


// Macros.
/** @brief The most options a choice holds, from a list or a provider (more are not shown). */
#define UI_CHOICE_MAX_OPTIONS ((size_t)1000000)
/** @brief The longest option text, in bytes: a longer one is cut at a whole character. */
#define UI_CHOICE_MAX_TEXT_BYTES ((size_t)1024)


// Types.
/**
 * @brief Tells how many options a choice has; asked whenever the element needs to know (each update while its list is open).
 * @param screen The screen.
 * @param widgetId The element.
 * @param userData The UserData given to UIChoice_SetProvider (the element's stored copy).
 * @returns The count; past UI_CHOICE_MAX_OPTIONS counts as that.
 */
typedef size_t (*UIChoiceCountProvider)(UIScreen* screen, uint64_t widgetId, const UserData* userData);

/**
 * @brief Writes the text of one option; asked for the rows an element shows.
 *
 * Runs on the update side; it must not change widgets, focus or the screen, only write the text.
 * @param screen The screen.
 * @param widgetId The element.
 * @param userData The UserData given to UIChoice_SetProvider.
 * @param index The option, below the count it told.
 * @param outText [out] A byte buffer to append the UTF-8 text to (no NUL needed; what it held stays before it). What it appends ends at
 *        its first NUL; text that is not valid UTF-8 shows nothing.
 * @returns Success, or an Error that stops the screen's update as a broken invariant.
 */
typedef Error (*UIChoiceTextProvider)(UIScreen* screen, uint64_t widgetId, const UserData* userData, size_t index, GenericBuffer* outText);

/**
 * @brief The choice piece's state. Embedded by an element's widget type; its fields are read-only outside the elements that hold it.
 */
typedef struct UIChoiceCapabilityStruct
{
    /** @brief The list's texts one after another, each NUL-terminated; owned, NULL with none or with providers. */
    unsigned char* _texts;
    /** @brief Where each of the list's texts starts in _texts; owned, _count of them. */
    size_t* _offsets;
    /** @brief How many options the list has (0 with providers). */
    size_t _count;
    /** @brief The providers, when the options come from them; both NULL for a list. */
    UIChoiceCountProvider _countProvider;
    UIChoiceTextProvider _textProvider;
    /** @brief What the providers are handed. */
    UserData _providerData;
    /** @brief Whether an option is selected. */
    bool _hasSelection;
    /** @brief The selected option, when _hasSelection. */
    size_t _selected;
    /** @brief Whether several things differ: shown as mixed whatever is selected. */
    bool _isMixed;
    /** @brief Moves on whenever the options or the selection change, so a look knows to refresh what it shows. */
    uint64_t _revision;
} UIChoiceCapability;


// Calling code: by widget id.
/**
 * @brief Gives a choice a list of options (copied), in place of what it had; a selection past the new list's end is cleared.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element (one that holds the piece: a dropdown).
 * @param texts The options' texts as UTF-8; may be NULL only when @p count is 0. Each must not be NULL.
 * @param count How many; at most UI_CHOICE_MAX_OPTIONS.
 * @returns Success; ErrorCode_IllegalArgument for a NULL text; ErrorCode_InvalidOperation for a stale id or a widget with no choice;
 *          ErrorCode_ArgumentOutOfRange for too many options or a text past UI_CHOICE_MAX_TEXT_BYTES; ErrorCode_InvalidTextEncoding for a
 *          text that is not valid UTF-8. Nothing changes on a failure.
 */
Error UIChoice_SetOptions(UIScreen* screen, uint64_t widgetId, const unsigned char* const* texts, size_t count);

/**
 * @brief Gives a choice options from providers, in place of what it had; a selection past their count is cleared.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param countProvider Tells how many; must not be NULL.
 * @param textProvider Writes one's text; must not be NULL.
 * @param userData Handed to both; copied. May be NULL for none.
 * @returns Success; ErrorCode_IllegalArgument for a NULL provider; ErrorCode_InvalidOperation for a stale id or a widget with no
 *          choice.
 */
Error UIChoice_SetProvider(UIScreen* screen, uint64_t widgetId, UIChoiceCountProvider countProvider, UIChoiceTextProvider textProvider,
    const UserData* userData);

/**
 * @brief Tells a choice that its provider's options changed (their texts, or how many there are): what shows them asks again, and a
 *        selection past the new count is cleared. A count that changes alone is noticed by an open list; texts that change under the
 *        same count need this.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no choice.
 */
Error UIChoice_Refresh(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Reads how many options a choice has (a provider is asked).
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param outCount [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no choice.
 */
Error UIChoice_GetCount(UIScreen* screen, uint64_t widgetId, size_t* outCount);

/**
 * @brief Appends the text of one option to a buffer (a provider is asked), NUL-terminated.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param index The option.
 * @param outText [out] A byte buffer to append to; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no choice; ErrorCode_ArgumentOutOfRange for an index past
 *          the count; ErrorCode_BufferTooLarge if the buffer could not grow; otherwise the provider's error.
 */
Error UIChoice_GetText(UIScreen* screen, uint64_t widgetId, size_t index, GenericBuffer* outText);

/**
 * @brief Selects an option, or none; the choice stops being mixed. Posts nothing.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param hasSelection Whether an option is selected.
 * @param index The option, when @p hasSelection; below the count.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no choice; ErrorCode_ArgumentOutOfRange for an index past
 *          the count, which changes nothing.
 */
Error UIChoice_SetSelected(UIScreen* screen, uint64_t widgetId, bool hasSelection, size_t index);

/**
 * @brief Reads the selection.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param outHasSelection [out] Receives whether an option is selected. Must not be NULL.
 * @param outIndex [out] Receives it, when one is; may be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no choice.
 */
Error UIChoice_GetSelected(UIScreen* screen, uint64_t widgetId, bool* outHasSelection, size_t* outIndex);

/**
 * @brief Makes a choice mixed (several things differ: shown as mixed) or not. Posts nothing.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param isMixed Whether it is.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no choice.
 */
Error UIChoice_SetMixed(UIScreen* screen, uint64_t widgetId, bool isMixed);

/**
 * @brief Reads whether a choice is mixed.
 * @param screen The element's screen; must not be NULL.
 * @param widgetId The element.
 * @param outIsMixed [out] Receives it. Must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation for a stale id or a widget with no choice.
 */
Error UIChoice_IsMixed(UIScreen* screen, uint64_t widgetId, bool* outIsMixed);


// The capability, for the elements that hold it.
/**
 * @brief Starts the piece with a list of options (copied) and a selection.
 * @param self The piece; must not be NULL.
 * @param texts The texts; may be NULL only when @p count is 0.
 * @param count How many; at most UI_CHOICE_MAX_OPTIONS.
 * @param hasSelection Whether an option is selected.
 * @param selected The option, when @p hasSelection; below @p count.
 * @returns Success; the errors of UIChoice_SetOptions, and ErrorCode_ArgumentOutOfRange for a selection past the list; nothing to release
 *          on a failure.
 */
Error UIChoiceCapability_Construct(UIChoiceCapability* self, const unsigned char* const* texts, size_t count, bool hasSelection, size_t selected);

/**
 * @brief Releases the piece.
 * @param self The piece; must not be NULL.
 */
void UIChoiceCapability_Deconstruct(UIChoiceCapability* self);

/**
 * @brief Returns how many options there are (a provider is asked, its answer kept to UI_CHOICE_MAX_OPTIONS).
 * @param self The piece; must not be NULL.
 * @param widget The element's widget; must not be NULL.
 * @returns The count.
 */
size_t UIChoiceCapability_GetCount(const UIChoiceCapability* self, const Widget* widget);

/**
 * @brief Appends one option's text to a buffer, NUL-terminated: what fits of it (cut at a whole character), nothing for text that is not
 *        valid UTF-8 or an index past the count.
 * @param self The piece; must not be NULL.
 * @param widget The element's widget; must not be NULL.
 * @param index The option.
 * @param outText [out] A byte buffer to append to; must not be NULL.
 * @returns Success; ErrorCode_BufferTooLarge if the buffer could not grow; otherwise the provider's error.
 */
Error UIChoiceCapability_AppendText(const UIChoiceCapability* self, const Widget* widget, size_t index, GenericBuffer* outText);

/**
 * @brief Chooses an option as the user did: it is selected and the choice stops being mixed, posting UIElementEvent_Changed and then
 *        UIElementEvent_Committed (the index as a double) on the element unless nothing changed.
 * @param self The piece; must not be NULL.
 * @param widget The element's widget; must not be NULL.
 * @param index The option; one past the count changes nothing.
 * @param outHasChanged [out] Set to whether anything changed; may be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIChoiceCapability_Choose(UIChoiceCapability* self, Widget* widget, size_t index, bool* outHasChanged);

/**
 * @brief Finds the next option after @p from whose text starts with a letter (letters of the Latin alphabet in either case alike),
 *        wrapping around. Asks for each text in turn: a provider of many options is asked many times.
 * @param self The piece; must not be NULL.
 * @param widget The element's widget; must not be NULL.
 * @param codepoint The letter.
 * @param hasFrom Whether to start after @p from; otherwise from the first.
 * @param from The option to start after.
 * @param scratch A byte buffer to read texts into; must not be NULL. Emptied by the call.
 * @param outIndex [out] Receives the option when one is found. Must not be NULL.
 * @param outIsFound [out] Set to whether one was. Must not be NULL.
 * @returns Success, or the provider's or the buffer's error.
 */
Error UIChoiceCapability_FindByLetter(const UIChoiceCapability* self, const Widget* widget, uint32_t codepoint, bool hasFrom, size_t from,
    GenericBuffer* scratch, size_t* outIndex, bool* outIsFound);
