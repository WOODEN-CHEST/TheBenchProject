#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRUserData.h"
// Vector2 is a place on the screen in a drag's state and its queries.
#include "raylib/raylib.h"
#include "GameKey.h"


/**
 * @file UIDragDrop.h
 * @brief Drag and drop, a service of the screen: one drag at a time per screen, from a widget that declared itself a source to one that
 *        declared itself a target, wherever they stand on the screen (in two windows, in two scroll areas).
 *
 * SOURCES declare themselves with UIDragDrop_SetSource: a provider that, when a press on the source (or on anything inside it that is not
 * a source of its own) moves past UI_DRAG_START_DISTANCE UI pixels, tells what would be dragged from the place the press began: a
 * payload (a kind, a value and a text to show), or nothing (a press on a part that does not drag). Elements that drag (a list's rows)
 * declare themselves. A hidden-cursor drag (UIScreen_CapturePointer) never becomes one.
 *
 * THE DRAG takes the press over: the widget that held it is told it lost it (OnPressLost), so a button pressed to start a drag does not
 * activate. From then on the screen follows the pointer, past the screen's edge too: the target is the top-most widget under the pointer
 * that declared itself one (UIDragDrop_SetTarget), or the nearest such ancestor of that widget, asked at each move whether it would take
 * the payload there (its acceptor). The pointer shows the arrow over a target that would take it and the not-allowed shape elsewhere; a
 * widget of the type the factory names (UIWidgetFactory_SetDragGhostType; the kit registers one) shows the payload's text beside the
 * pointer, and a frame over the target that would take it, in the overlay layer. Elements show more of their own: a list where the drop
 * would go (UIDragDrop_GetTargetState).
 *
 * IT ENDS at the press's release: over a target that would take it, a UIEventKind_Drag notification UIDragEvent_Dropped goes to the target
 * and UIDragEvent_Delivered to the source, each with the drag's record (UIEvent.Drag: the payload's kind and value, both widgets, the
 * place and the modifiers held); anywhere else UIDragEvent_Cancelled goes to the source. Escape (the back action), UIDragDrop_Cancel and
 * the source going (destroyed, detached, hidden) cancel it too; a target that goes is forgotten, and the pointer's place picks another.
 * The events are delivered at the end of the update as all notifications are, and like a widget's own events even when their widget was
 * destroyed after they were posted.
 *
 * A side table per screen, emptied of a widget at its teardown: widgets that do not drag cost nothing.
 */


// Forward declarations.
/** @brief A UI screen; full type in UIScreen.h. */
typedef struct UIScreenStruct UIScreen;


// Macros.
/** @brief The most bytes of a payload's text, its NUL included. */
#define UI_DRAG_MAX_TEXT_BYTES ((size_t)128)
/** @brief How far a press must move, in UI pixels, before it drags. */
#define UI_DRAG_START_DISTANCE 4.0f


// Types.
/**
 * @brief What is dragged: a kind, which targets decide by, a value, which tells which one, and a text the dragged item shows. A plain
 *        value; fill its text with UIDragPayload_SetText or UIDragPayload_SetTextCount.
 */
typedef struct UIDragPayloadStruct
{
    /** @brief What sort of thing it is, a number the caller chooses (a key id: "tbp:asset", "tbp:object"). */
    uint32_t Kind;
    /** @brief Which one: an object's id, a row's data index. */
    uint64_t Value;
    /** @brief What the dragged item shows: NUL-terminated UTF-8, cut at a codepoint to fit. */
    unsigned char Text[UI_DRAG_MAX_TEXT_BYTES];
} UIDragPayload;

/**
 * @brief Tells what a press on a source would drag, when it has moved far enough.
 *
 * Runs in the screen's input routing; it must not change widgets, focus or the screen, only write @p outPayload.
 * @param screen The screen.
 * @param sourceId The widget that declared itself the source.
 * @param pressScreenPosition Where the press began, in normalized screen coordinates.
 * @param userData The UserData given to UIDragDrop_SetSource.
 * @param outPayload [out] What would be dragged; its kind must not be 0. Starts empty (kind 0, value 0, no text).
 * @param outIsDragged [out] Set when it drags; left false, the press goes on as a press.
 * @returns Success, or an Error that stops the screen's update as a broken invariant.
 */
typedef Error (*UIDragSourceProvider)(UIScreen* screen, uint64_t sourceId, Vector2 pressScreenPosition, const UserData* userData,
    UIDragPayload* outPayload, bool* outIsDragged);

/**
 * @brief What a target is asked: would it take this payload, here.
 */
typedef struct UIDragQueryStruct
{
    /** @brief What is dragged; borrowed for the call. */
    const UIDragPayload* Payload;
    /** @brief Where it comes from. */
    uint64_t SourceId;
    /** @brief The widget that declared itself the target. */
    uint64_t TargetId;
    /** @brief Where the pointer is, in normalized screen coordinates. */
    Vector2 ScreenPosition;
    /** @brief The GAME_KEY_MODIFIER_* flags held. */
    uint8_t Modifiers;
} UIDragQuery;

/**
 * @brief Tells whether a target would take a payload.
 *
 * Runs in the screen's update, at each move of the pointer and each update; it must not change widgets, focus or the screen.
 * @param screen The screen.
 * @param query The question; borrowed for the call.
 * @param userData The UserData given to UIDragDrop_SetTarget.
 * @returns true if it would take it.
 */
typedef bool (*UIDragAcceptor)(UIScreen* screen, const UIDragQuery* query, const UserData* userData);

/**
 * @brief How a widget stands toward the drag in progress.
 */
typedef enum UIDragTargetStateEnum
{
    /** @brief It is not the target: no drag, or the pointer is elsewhere. */
    UIDragTargetState_None,
    /** @brief It is the target, and would take the drop. */
    UIDragTargetState_Accepting,
    /** @brief It is the target, and would not. */
    UIDragTargetState_Refusing
} UIDragTargetState;

/**
 * @brief The drag in progress, as read by UIDragDrop_GetSession. A plain value.
 */
typedef struct UIDragSessionStruct
{
    /** @brief What is dragged. */
    UIDragPayload Payload;
    /** @brief Where it comes from. */
    uint64_t SourceId;
    /** @brief The target under the pointer, or WIDGET_ID_INVALID. */
    uint64_t TargetId;
    /** @brief Whether that target would take it. */
    UIDragTargetState TargetState;
    /** @brief Where the pointer is, in normalized screen coordinates (past [0;1] off the screen). */
    Vector2 ScreenPosition;
    /** @brief The mouse button that drags. */
    GameKey Button;
    /** @brief The GAME_KEY_MODIFIER_* flags held at the last update. */
    uint8_t Modifiers;
} UIDragSession;

/**
 * @brief What a UIEventKind_Drag notification tells (its WidgetEventId).
 */
typedef enum UIDragEventEnum
{
    /** @brief To the target: the drag was dropped on it. */
    UIDragEvent_Dropped,
    /** @brief To the source: what it gave was dropped on a target. */
    UIDragEvent_Delivered,
    /** @brief To the source: the drag ended with no drop (let go where nothing took it, Escape, code, or a target that went). */
    UIDragEvent_Cancelled
} UIDragEvent;

/**
 * @brief What a UIEventKind_Drag notification carries by value (UIEvent.Drag). A plain value.
 */
typedef struct UIDragRecordStruct
{
    /** @brief The payload's kind. */
    uint32_t Kind;
    /** @brief The payload's value. */
    uint64_t Value;
    /** @brief Where it came from. */
    uint64_t SourceId;
    /** @brief Where it was dropped, or WIDGET_ID_INVALID when it was not. */
    uint64_t TargetId;
    /** @brief Where the pointer was, in normalized screen coordinates. */
    Vector2 ScreenPosition;
    /** @brief The GAME_KEY_MODIFIER_* flags held (Ctrl to copy rather than move, as a target decides). */
    uint8_t Modifiers;
} UIDragRecord;

/**
 * @brief The screen's drag state. Embedded in the screen; read it with UIDragDrop_GetSession.
 */
typedef struct UIScreenDragStruct
{
    /** @brief Whether a drag is in progress. */
    bool IsActive;
    /** @brief What it drags, from where, toward what. */
    UIDragPayload Payload;
    uint64_t SourceId;
    uint64_t TargetId;
    UIDragTargetState TargetState;
    /** @brief The press it took over: its button's index among the mouse buttons. */
    size_t ButtonIndex;
    /** @brief The pointer's place and the modifiers at the last update. */
    Vector2 ScreenPosition;
    uint8_t Modifiers;
    /** @brief The widget that shows it (a root of the overlay layer), or WIDGET_ID_INVALID until one is needed. */
    uint64_t DisplayId;
} UIScreenDrag;


// Functions.
/**
 * @brief Writes a payload's text: a copy of @p byteCount bytes of UTF-8 (to its first NUL), cut at a codepoint to fit; text that is not
 *        valid UTF-8 is left out.
 * @param self The payload; must not be NULL.
 * @param text The text; may be NULL for none.
 * @param byteCount How many bytes of it.
 * @returns Success.
 */
Error UIDragPayload_SetText(UIDragPayload* self, const unsigned char* text, size_t byteCount);

/**
 * @brief Writes a payload's text as a count and a word: "3 rows".
 * @param self The payload; must not be NULL.
 * @param count The count.
 * @param noun The word, UTF-8; must not be NULL.
 * @returns Success.
 */
Error UIDragPayload_SetTextCount(UIDragPayload* self, size_t count, const unsigned char* noun);

/**
 * @brief Declares a widget a drag source, or replaces its provider.
 * @param screen The screen; must not be NULL.
 * @param widgetId An alive widget of the screen.
 * @param provider Tells what a press drags; must not be NULL.
 * @param userData Handed to the provider; may be NULL. Copied.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidOperation for a widget that is not alive;
 *          ErrorCode_BufferTooLarge if the table could not grow.
 */
Error UIDragDrop_SetSource(UIScreen* screen, uint64_t widgetId, UIDragSourceProvider provider, const UserData* userData);

/**
 * @brief Makes a widget no drag source (nothing happens for one that is none). A drag it started goes on.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 */
void UIDragDrop_ClearSource(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Declares a widget a drop target, or replaces its acceptor.
 * @param screen The screen; must not be NULL.
 * @param widgetId An alive widget of the screen.
 * @param acceptor Tells whether it would take a payload; must not be NULL.
 * @param userData Handed to the acceptor; may be NULL. Copied.
 * @returns Success; ErrorCode_IllegalArgument if a pointer is NULL; ErrorCode_InvalidOperation for a widget that is not alive;
 *          ErrorCode_BufferTooLarge if the table could not grow.
 */
Error UIDragDrop_SetTarget(UIScreen* screen, uint64_t widgetId, UIDragAcceptor acceptor, const UserData* userData);

/**
 * @brief Makes a widget no drop target (nothing happens for one that is none); the drag in progress looks for its target again.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 */
void UIDragDrop_ClearTarget(UIScreen* screen, uint64_t widgetId);

/**
 * @brief Returns whether a drag is in progress.
 * @param screen The screen; must not be NULL.
 * @returns true while one is.
 */
bool UIDragDrop_IsDragging(const UIScreen* screen);

/**
 * @brief Reads the drag in progress.
 * @param screen The screen; must not be NULL.
 * @param outSession [out] Receives it; must not be NULL. Left as it was with no drag.
 * @returns true while a drag is in progress.
 */
bool UIDragDrop_GetSession(const UIScreen* screen, UIDragSession* outSession);

/**
 * @brief Returns how a widget stands toward the drag in progress: the target that would take it, the target that would not, or neither.
 * @param screen The screen; must not be NULL.
 * @param widgetId The widget.
 * @returns The state.
 */
UIDragTargetState UIDragDrop_GetTargetState(const UIScreen* screen, uint64_t widgetId);

/**
 * @brief Ends the drag in progress with no drop: UIDragEvent_Cancelled to its source. Nothing happens with none.
 * @param screen The screen; must not be NULL.
 * @returns Success, or the event queue's error.
 */
Error UIDragDrop_Cancel(UIScreen* screen);
