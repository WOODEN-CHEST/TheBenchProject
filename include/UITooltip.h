#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
// GenericBuffer is what a provider writes into, UserData what it is handed, Vector2 where the pointer is.
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"
#include "raylib/raylib.h"


/**
 * @file UITooltip.h
 * @brief Hover text: a text shown beside the pointer after it rests on a widget, as a service of the screen.
 *
 * Any widget can have a hover text, by value (UITooltip_Set) or from a provider asked while it shows (UITooltip_SetProvider), with
 * no widget type embedding anything: the screen keeps a side table, emptied when the widget is torn down, so a widget without one
 * costs nothing. A widget with none falls back to its ancestors' (a compound's parts show the compound's text).
 *
 * Each update, after the input, the screen works out where the pointer rests: the hovered widget, or the nearest ancestor with a
 * hover text. Once it has rested there for the delay (its own, UITooltip_SetDelay, else the theme's tooltip_delay) the text shows;
 * moving onto something with another hover text, or off the screen, hides it and starts again, and a mouse press hides it until the
 * pointer moves off. A status bar can read the same text at once (UITooltip_GetCurrent).
 *
 * It shows as a widget of the type the screen's factory names (UIWidgetFactory_SetTooltipType; the kit registers one, themed by the
 * theme's tooltip kind), made the first time a text shows, as a root in the overlay layer: it takes no input, sits beside the
 * pointer and keeps on the surface. Without a type nothing shows, and the rest still works.
 */


// Forward declarations.
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;


// Macros.
/** @brief The longest hover text, in bytes: a longer one (a provider's) is cut at a whole character. */
#define UI_TOOLTIP_MAX_TEXT_BYTES ((size_t)4096)
/** @brief The longest delay a widget's own may be, in seconds. */
#define UI_TOOLTIP_MAX_DELAY_SECONDS 10.0f


// Types.
/**
 * @brief Gives a widget's hover text while it shows: asked each update while the pointer rests on the widget (or on what falls back
 *        to it).
 *
 * Runs on the update side, after the input; it must not change widgets, focus or the screen, only write the text.
 * @param screen The screen.
 * @param widgetId The widget whose hover text it is.
 * @param userData The UserData given to UITooltip_SetProvider (the screen's stored copy).
 * @param outText [out] A byte buffer, emptied before the call, to append the text to as UTF-8 (no NUL needed: the text ends at the
 *        first NUL there is). An empty text shows nothing; text that is not valid UTF-8 shows nothing.
 * @returns Success, or an Error that stops the screen's update as a broken invariant.
 */
typedef Error (*UITooltipProvider)(UIScreen* screen, uint64_t widgetId, const UserData* userData, GenericBuffer* outText);

/**
 * @brief Where the pointer rests and the hover text there, for a status bar or the widget that shows it. A plain value.
 */
typedef struct UITooltipStateStruct
{
    /** @brief The widget whose hover text it is: the hovered widget, or the nearest ancestor with one. */
    uint64_t WidgetId;
    /** @brief The text, NUL-terminated UTF-8; the screen's, valid until its next update. */
    const unsigned char* Text;
    /** @brief Whether the delay has passed and nothing hid it: the text shows. */
    bool IsShown;
    /** @brief Where the pointer is, normalized [0;1] screen coordinates. */
    Vector2 PointerPosition;
} UITooltipState;


// Functions.
/**
 * @brief Gives a widget a hover text by value, in place of one it had.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget; must be alive.
 * @param text The text as UTF-8; must not be NULL, at most UI_TOOLTIP_MAX_TEXT_BYTES. Copied. An empty text shows nothing.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidOperation if the widget is not alive;
 *          ErrorCode_InvalidTextEncoding if the text is not valid UTF-8; ErrorCode_ArgumentOutOfRange if it is too long;
 *          ErrorCode_BufferTooLarge if the table could not grow.
 */
Error UITooltip_Set(UIScreen* screen, uint64_t widgetId, const unsigned char* text);

/**
 * @brief Gives a widget a hover text from a provider, asked while it shows, in place of one it had.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget; must be alive.
 * @param provider The provider; must not be NULL.
 * @param userData Handed to the provider; copied. May be NULL for none.
 * @returns Success; ErrorCode_IllegalArgument if @p screen or @p provider is NULL; ErrorCode_InvalidOperation if the widget is not
 *          alive; ErrorCode_BufferTooLarge if the table could not grow.
 */
Error UITooltip_SetProvider(UIScreen* screen, uint64_t widgetId, UITooltipProvider provider, const UserData* userData);

/**
 * @brief Gives a widget's hover text a delay of its own, or the theme's again.
 * @param screen The screen; must not be NULL.
 * @param widgetId A widget with a hover text.
 * @param hasDelay Whether it has its own delay; without, the theme's tooltip_delay counts.
 * @param delaySeconds Its own delay, when @p hasDelay; within [0, UI_TOOLTIP_MAX_DELAY_SECONDS].
 * @returns Success; ErrorCode_IllegalArgument if @p screen is NULL; ErrorCode_InvalidOperation if the widget has no hover text;
 *          ErrorCode_ArgumentOutOfRange for a delay out of bounds.
 */
Error UITooltip_SetDelay(UIScreen* screen, uint64_t widgetId, bool hasDelay, float delaySeconds);

/**
 * @brief Takes a widget's hover text away; it falls back to its ancestors' again. Nothing happens to a widget without one.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 * @returns Success; ErrorCode_IllegalArgument if @p screen is NULL.
 */
Error UITooltip_Clear(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Reads where the pointer rests and its hover text, as of the screen's last update (a widget destroyed or cleared since
 *        takes its text with it at once).
 * @param screen The screen; must not be NULL.
 * @param outState [out] Receives it when there is one; must not be NULL.
 * @returns true when the pointer rests on something with a hover text that is not empty, shown or still waiting for its delay.
 */
bool UITooltip_GetCurrent(const UIScreen* screen, UITooltipState* outState);
