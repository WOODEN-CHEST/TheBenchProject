#pragma once
#include "WRMemory.h"
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"
#include "WRUserData.h"


/**
 * The module provides a way to subscribe to and raise events.
 * The event class is designed such that it can be subscribed or unsubscribed from at any time,
 * including in the middle of an event being raised.
 * However, the event class is not thread-safe.
 *
 * Events are reentrant: a handler invoked by WREvent_Raise may itself call WREvent_Raise on the same
 * event (directly or indirectly). Each raise snapshots the current set of subscribers and iterates
 * only that snapshot, so a subscribe or unsubscribe performed inside a handler affects future raises
 * but never the raise in progress, and recursive re-raising is safe.
 *
 * All operations on a single WREvent must be confined to one thread; the type is NOT thread-safe.
 */


/**
 * @brief Sentinel handle value that never identifies a real subscriber.
 *
 * Returned-by / compared-against handles use this value to mean "no subscriber". A valid handle from
 * WREvent_Subscribe is never equal to this. It is rejected by WREvent_Unsubscribe.
 */
#define WREVENT_HANDLE_INVALID ((WREventHandle)0)

/**
 * @brief Default subscriber priority.
 *
 * A convenient neutral priority to pass to WREvent_Subscribe when no specific ordering is required.
 * Subscribers with numerically higher priority are invoked before those with lower priority.
 */
#define WREVENT_PRIORITY_DEFAULT ((WREventPriority)0)

// Types.
/**
 * @brief Opaque identifier for a single subscription to an event.
 *
 * Returned by WREvent_Subscribe and passed back to WREvent_Unsubscribe to remove that specific
 * subscription. Handles are unique within one event for the lifetime of that event. The value
 * WREVENT_HANDLE_INVALID is reserved and never names a live subscription.
 */
typedef uint64_t WREventHandle;

/**
 * @brief Relative ordering value for a subscriber.
 *
 * Determines the order in which handlers run when an event is raised: subscribers with a numerically
 * higher priority are invoked before those with a lower priority. Subscribers added with equal
 * priority are invoked in the order they were subscribed.
 */
typedef int64_t WREventPriority;

/**
 * @brief Bundle of information delivered to a handler each time an event is raised.
 *
 * A fresh WREventArgs is constructed by WREvent_Raise for every subscriber invocation. It carries the
 * UserData that was attached when that subscriber subscribed plus the per-raise event payload pointer.
 * Handlers should read it through WREventArgs_GetUserData / WREventArgs_GetEventArgs rather than
 * touching the fields directly.
 */
typedef struct WREventArgsStruct
{
    /** @brief Copy of the UserData supplied when the receiving subscriber subscribed. */
    UserData _userData;
    /** @brief The opaque payload pointer passed to WREvent_Raise; ownership stays with the raiser. */
    void* _eventArgs;
} WREventArgs;

/**
 * @brief Signature of a function that handles a raised event.
 *
 * Invoked once per matching subscriber by WREvent_Raise, in priority order. The handler receives the
 * per-invocation WREventArgs (valid only for the duration of the call). Returning a non-success Error
 * stops the raise: no further subscribers for that raise are invoked and the error is propagated out
 * of WREvent_Raise. A handler may subscribe to or unsubscribe from the event, including re-raising the
 * same event, without disrupting the raise that is currently running.
 * @param args Per-invocation arguments (subscriber UserData and event payload). Non-NULL; valid only
 *        during the call.
 * @returns Success to allow the raise to continue, or a non-success Error to abort the raise and have
 *          that Error returned from WREvent_Raise.
 */
typedef Error (*WREventHandlerFunction)(WREventArgs* args);

/**
 * @brief A single registered subscription: a handler plus its handle, priority, and UserData.
 *
 * Internal record stored by the event for each active subscription. Field layout is exposed because
 * the caller supplies the backing buffers (see WREvent_Construct2), but callers should treat its
 * contents as managed by the WREvent_* functions.
 */
typedef struct WREventSubscriberStruct
{
    /** @brief Unique handle identifying this subscription within its event. */
    uint64_t _handle;
    /** @brief The function invoked when the event is raised for this subscriber. */
    WREventHandlerFunction _handler;
    /** @brief Ordering value controlling when this handler runs relative to others. */
    WREventPriority _priority;
    /** @brief Copy of the caller-attached UserData handed to the handler on each raise. */
    UserData _userData;
} WREventSubscriber;

/**
 * @brief A subscribable, reentrant (but not thread-safe) event with priority-ordered subscribers.
 *
 * Holds the list of current subscribers and the scratch storage used to snapshot them during a raise.
 * Construct with WREvent_Construct1 (the event owns internally allocated buffers) or WREvent_Construct2
 * (the caller supplies the buffers), and release with WREvent_Deconstruct. All fields are managed by
 * the WREvent_* functions and should not be modified directly.
 */
typedef struct WREventStruct
{
    /** @brief The handle that will be assigned to the next subscriber added. */
    WREventHandle _nextAvailableHandle;
    /** @brief Internally owned subscriber buffer used when constructed with WREvent_Construct1. */
    GenericBuffer _selfSubscribers;
    /** @brief Internally owned snapshot buffer used when constructed with WREvent_Construct1. */
    GenericBuffer _selfRaiseSubscriberSnapshot;
    /** @brief Points to the subscriber buffer actually in use (internal or caller-supplied). */
    GenericBuffer* _activeSubscribers;
    /** @brief Points to the per-raise snapshot buffer actually in use (internal or caller-supplied). */
    GenericBuffer* _activeRaiseSubscriberSnapshot;
    /** @brief true if the buffers were allocated by the event and must be freed on deconstruct. */
    bool _areBuffersOwned;
} WREvent;


// Functions.
/**
 * @brief Constructs an event that owns its own internally allocated subscriber storage.
 *
 * Initializes the event with internally allocated subscriber and snapshot buffers that grow as needed.
 * The resulting event is ready to be subscribed to and raised. Must be released with WREvent_Deconstruct,
 * which frees the internally owned buffers.
 * @param self The event to construct. Must not be NULL.
 * @returns Success once the event is ready. ErrorCode_IllegalArgument if self is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WREvent_Construct1(WREvent* self);

/**
 * @brief Constructs an event that uses two caller-supplied buffers for its storage.
 *
 * Initializes the event to use the provided buffers for the active subscriber list and the per-raise
 * snapshot, instead of allocating its own. Both buffers are CLEARED before use, so any existing
 * contents are discarded. The buffers must remain valid for the lifetime of the event, must each store
 * WREventSubscriber-sized elements, must be writable (not read-only), and must be two distinct buffers.
 * Because the buffers are not owned by the event, WREvent_Deconstruct does not free them.
 * @param self The event to construct. Must not be NULL.
 * @param subscriberBuffer Buffer used to hold the active subscriber list. Must not be NULL, must be
 *        writable, and must have an element size of sizeof(WREventSubscriber).
 * @param subscriberSnapshotBuffer Buffer used as scratch space to snapshot subscribers during a raise.
 *        Must not be NULL, must be writable, must have an element size of sizeof(WREventSubscriber), and
 *        must be a different buffer than subscriberBuffer.
 * @returns Success once the event is ready. ErrorCode_IllegalArgument if self is NULL, if either buffer
 *          is NULL / read-only / has the wrong element size, or if the two buffers are the same buffer;
 *          ErrorCode_InvalidOperation if either buffer could not be cleared.
 */
Error WREvent_Construct2(WREvent* self, GenericBuffer* subscriberBuffer, GenericBuffer* subscriberSnapshotBuffer);

/**
 * @brief Releases an event, freeing any internally owned storage and resetting it.
 *
 * Frees the subscriber and snapshot buffers only if they were allocated by the event itself (i.e. it
 * was created with WREvent_Construct1); buffers supplied through WREvent_Construct2 are left untouched
 * and remain the caller's responsibility. The event is reset to an empty, unusable state afterwards.
 * Safe to call on a NULL pointer.
 * @param self The event to deconstruct. May be NULL, in which case this is a no-op.
 */
void WREvent_Deconstruct(WREvent* self);

/**
 * @brief Registers a handler to be invoked when the event is raised.
 *
 * Adds a new subscription with the given handler, priority, and UserData, and returns a handle that
 * identifies it. The UserData is copied by value into the subscription and a pointer to that stored
 * copy is later passed to the handler on every raise. The subscriber is placed according to its
 * priority (higher priority runs earlier; equal priorities preserve subscription order). Subscribing
 * from within a handler is allowed and takes effect on subsequent raises, not the one in progress.
 * @param self The event to subscribe to. Must not be NULL and must have been constructed.
 * @param handler The function to invoke on each raise. Must not be NULL.
 * @param priority The ordering value for this subscriber relative to others.
 * @param userData Pointer to a UserData value to copy into the subscription. May be NULL, in which case
 *        an empty (all-zero) UserData is stored.
 * @param outHandle [out] Receives the handle identifying the new subscription. Must not be NULL.
 * @returns Success with *outHandle populated. ErrorCode_IllegalArgument if self, handler, or outHandle
 *          is NULL; ErrorCode_InvalidOperation if the event has not been constructed or has exhausted
 *          its supply of handles; ErrorCode_BufferTooSmall if the subscriber could not be stored.
 */
Error WREvent_Subscribe(WREvent* self,
    WREventHandlerFunction handler,
    WREventPriority priority,
    const UserData* userData,
    WREventHandle* outHandle);

/**
 * @brief Removes the single subscription identified by the given handle.
 *
 * Detaches the subscriber previously registered under handle so its handler will no longer be invoked
 * on future raises. Unsubscribing from within a handler is allowed and takes effect on subsequent
 * raises, not the one in progress.
 * @param self The event to modify. Must not be NULL and must have been constructed.
 * @param handle The handle returned by WREvent_Subscribe for the subscription to remove. Must not be
 *        WREVENT_HANDLE_INVALID.
 * @returns Success once the subscriber is removed. ErrorCode_IllegalArgument if self is NULL or handle
 *          is WREVENT_HANDLE_INVALID; ErrorCode_InvalidOperation if the event has not been constructed,
 *          if no subscriber with that handle exists, or if the removal failed.
 */
Error WREvent_Unsubscribe(WREvent* self, WREventHandle handle);

/**
 * @brief Removes every subscription from the event.
 *
 * Clears the entire subscriber list so no handlers remain. Existing handles become invalid. Like a
 * single unsubscribe, calling this from within a handler affects only subsequent raises.
 * @param self The event to clear. Must not be NULL and must have been constructed.
 * @returns Success once all subscribers are removed. ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the event has not been constructed or the list could not be
 *          cleared.
 */
Error WREvent_UnsubscribeAll(WREvent* self);

/**
 * @brief Returns the number of subscribers currently registered on the event.
 *
 * Reports the count of active subscriptions at the moment of the call.
 * @param self The event to query. May be NULL or unconstructed, in which case 0 is returned.
 * @returns The number of current subscribers, or 0 if self is NULL or not constructed.
 */
size_t WREvent_GetSubscriberCount(WREvent* self);

/**
 * @brief Invokes every subscribed handler in priority order with the given payload.
 *
 * Takes a snapshot of the current subscribers and invokes each one in priority order (higher priority
 * first; ties in subscription order), passing each handler a WREventArgs carrying that subscriber's
 * UserData and the supplied eventArgs payload. Because the subscriber set is snapshotted per raise,
 * handlers may subscribe, unsubscribe, or re-raise this same event without affecting the raise in
 * progress, making recursion safe. If any handler returns a non-success Error, the raise stops
 * immediately (remaining handlers are not invoked) and that Error is returned.
 * @param self The event to raise. Must not be NULL and must have been constructed.
 * @param eventArgs Opaque payload pointer forwarded to every handler via the event arguments. May be
 *        NULL; ownership remains with the caller and the event neither dereferences nor frees it.
 * @returns Success if all handlers ran and returned success (or there were no subscribers).
 *          ErrorCode_IllegalArgument if self is NULL; ErrorCode_InvalidOperation if the event has not
 *          been constructed; ErrorCode_BufferTooSmall if the subscriber snapshot could not be created;
 *          otherwise the first non-success Error returned by a handler.
 */
Error WREvent_Raise(WREvent* self, void* eventArgs);


/**
 * @brief Returns the event payload pointer carried by the event arguments.
 *
 * Convenience accessor for the payload that was passed to WREvent_Raise, for use inside a handler.
 * @param args The event arguments received by a handler. Must not be NULL.
 * @returns The opaque eventArgs pointer supplied to WREvent_Raise (may be NULL). Ownership is unchanged.
 */
static inline void* WREventArgs_GetEventArgs(WREventArgs* args)
{
    return args->_eventArgs;
}

/**
 * @brief Returns a pointer to the subscriber UserData carried by the event arguments.
 *
 * Convenience accessor for the UserData that was attached when the receiving handler subscribed, for
 * use inside the handler.
 * @param args The event arguments received by a handler. Must not be NULL.
 * @returns A read-only pointer to the subscriber's UserData, valid for the duration of the handler call.
 */
static inline const UserData* WREventArgs_GetUserData(WREventArgs* args)
{
    return &args->_userData;
}
