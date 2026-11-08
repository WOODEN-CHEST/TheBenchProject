#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
// UserData is what a provider is handed, Vector2 where the menu opens.
#include "wr/WRUserData.h"
#include "raylib/raylib.h"


/**
 * @file UIContextMenu.h
 * @brief Context menus as a service of the screen: a widget names a provider, and a context request on it or on anything inside it
 *        asks the nearest provider up the tree to open a menu.
 *
 * Any widget can have a provider (UIContextMenu_SetProvider), with no widget type embedding anything: the screen keeps a side table,
 * emptied when the widget is torn down, so a widget without one costs nothing. A context request walks up from the widget it is for to
 * the first one with a provider and calls it with the request (which widget it was for, which widget's provider answers, and where:
 * the pointer, or the widget's middle for a key); the provider builds and opens the menu, usually with the kit's UIMenu_Open at the
 * request's place (UIMenu.h), or does nothing. A tab gives Undock, a slider Reset, Copy and Paste.
 *
 * The screen makes a request on its own for a right press let go on the widget it began on, and for a press of the context menu action
 * (tbp:ui_context_menu) that no widget used, for the focused widget; the kit's elements make one when they take that action's key
 * themselves (UIElement_OnContextKey). Code can make one too (UIContextMenu_Request).
 */


// Forward declarations.
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;


// Types.
/**
 * @brief A context request, handed to the provider that answers it. A plain value.
 */
typedef struct UIContextMenuRequestStruct
{
    /** @brief The widget whose provider answers: the request's widget or the nearest ancestor with a provider. */
    uint64_t ProviderWidgetId;
    /** @brief The widget the request is for: the one pressed, or the focused one for the key. */
    uint64_t TargetWidgetId;
    /** @brief Where the menu opens, normalized [0;1] screen coordinates: the pointer, or the target's middle for a key. */
    Vector2 ScreenPosition;
    /** @brief Whether the request came from a key, so the menu can open with its first item focused. */
    bool IsFromKey;
} UIContextMenuRequest;

/**
 * @brief Answers a context request: builds a menu and opens it (UIMenu_Open), or does nothing.
 *
 * Runs on the update side, during the screen's input routing or from code, as a widget hook does: it may create and destroy widgets and
 * move the focus.
 * @param screen The screen.
 * @param request The request; valid for the call.
 * @param userData The UserData given to UIContextMenu_SetProvider (the screen's stored copy, copied out for the call).
 * @returns Success, or an Error that stops the screen's update as a broken invariant.
 */
typedef Error (*UIContextMenuProvider)(UIScreen* screen, const UIContextMenuRequest* request, const UserData* userData);


// Functions.
/**
 * @brief Gives a widget a context menu provider, in place of one it had.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget; must be alive.
 * @param provider The provider; must not be NULL.
 * @param userData Handed to the provider; copied. May be NULL for none.
 * @returns Success; ErrorCode_IllegalArgument if @p screen or @p provider is NULL; ErrorCode_InvalidOperation if the widget is not
 *          alive; ErrorCode_BufferTooLarge if the table could not grow.
 */
Error UIContextMenu_SetProvider(UIScreen* screen, uint64_t widgetId, UIContextMenuProvider provider, const UserData* userData);

/**
 * @brief Takes a widget's provider away: its requests go on up the tree. Nothing happens to a widget without one.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 * @returns Success; ErrorCode_IllegalArgument if @p screen is NULL.
 */
Error UIContextMenu_Clear(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Returns whether a widget has a provider of its own.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 * @returns true if it has.
 */
bool UIContextMenu_HasProvider(const UIScreen* screen, uint64_t widgetId);

/**
 * @brief Makes a context request for a widget: the nearest provider at it or up its ancestors answers it.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget the request is for.
 * @param screenPosition Where the menu opens, normalized [0;1] screen coordinates; finite.
 * @param isFromKey Whether a key made it.
 * @param outIsAnswered [out] Set to whether a provider was found and called; may be NULL.
 * @returns Success (also with no provider, or a widget that is gone); ErrorCode_IllegalArgument if @p screen is NULL;
 *          ErrorCode_ArgumentOutOfRange for a position that is not finite; otherwise the provider's error.
 */
Error UIContextMenu_Request(UIScreen* screen, uint64_t widgetId, Vector2 screenPosition, bool isFromKey, bool* outIsAnswered);
