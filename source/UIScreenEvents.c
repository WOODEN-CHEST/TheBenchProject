#include "UIScreen.h"
#include "UIInternal.h"
#include "UIWidget.h"
#include "Logger.h"
#include <inttypes.h>
#include "wr/WRMemory.h"
#include "wr/WRUserData.h"


// Types.
/* One subscription. Removed ones are only marked while notifications are delivered, then compacted. */
typedef struct SubscriptionStruct
{
    uint64_t Id;
    /* The watched widget, or WIDGET_ID_INVALID for every widget. */
    uint64_t WidgetId;
    uint32_t KindMask;
    UIEventHandler Handler;
    UserData Data;
    bool IsRemoved;
} Subscription;


// Static functions.
static Subscription* GetSubscriptionAt(UIScreen* self, size_t index)
{
    return GenericBuffer_GetPointerToElement(&self->_subscriptions, index);
}

static bool IsMatch(const Subscription* subscription, const UIEvent* event)
{
    return !subscription->IsRemoved
        && ((subscription->KindMask & UI_EVENT_KIND_MASK(event->Kind)) != 0U)
        && ((subscription->WidgetId == WIDGET_ID_INVALID) || (subscription->WidgetId == event->WidgetId));
}

/* A state change describes a widget that exists: once the widget is destroyed it is dropped. A widget's own event (a
 * button's "clicked") is delivered even if the widget was destroyed after posting it, since it carries everything by
 * value. */
static bool IsDeliverable(const UIScreen* self, const UIEvent* event)
{
    return (event->Kind == UIEventKind_Widget) || (UIScreen_InternalResolve(self, event->WidgetId) != NULL);
}

static void RemoveMarkedSubscriptions(UIScreen* self)
{
    for (size_t Index = self->_subscriptions._count; Index-- > 0U;)
    {
        if (GetSubscriptionAt(self, Index)->IsRemoved)
        {
            GenericBuffer_RemoveAt(&self->_subscriptions, Index);
        }
    }
}

/* Delivers one notification to every matching subscriber, while it stays deliverable (a handler may destroy the widget
 * of a state change: the rest do not hear about it). Subscriptions added meanwhile wait for the next notification. */
static Error DeliverEvent(UIScreen* self, const UIEvent* event)
{
    size_t SubscriptionCount = self->_subscriptions._count;
    for (size_t Index = 0; Index < SubscriptionCount; Index++)
    {
        if (!IsDeliverable(self, event))
        {
            return Error_CreateSuccess();
        }
        Subscription* Entry = GetSubscriptionAt(self, Index);
        if (!IsMatch(Entry, event))
        {
            continue;
        }
        // Copied out: a handler that subscribes can move the stored subscriptions.
        UIEventHandler Handler = Entry->Handler;
        UserData Data = Entry->Data;
        Error Result = Handler(self, event, &Data);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }
    return Error_CreateSuccess();
}


// Internal functions (declared in UIInternal.h).
void UIScreen_InternalInitializeEvents(UIScreen* self)
{
    GenericBuffer_AllocateVariable(&self->_eventQueue, 16U, sizeof(UIEvent));
    GenericBuffer_AllocateVariable(&self->_subscriptions, 4U, sizeof(Subscription));
    self->_nextSubscriptionId = 1U;
}

bool UIScreen_InternalPostEvent(UIScreen* self, const UIEvent* event)
{
    if (self->_isTearingDown)
    {
        return true;
    }
    UIEvent Copy = *event;
    return GenericBuffer_AddLast(&self->_eventQueue, &Copy);
}

Error UIScreen_InternalDispatchEvents(UIScreen* self)
{
    self->_isDispatching = true;
    Error Result = Error_CreateSuccess();
    size_t DeliveredCount = 0;
    bool IsOverflowing = false;
    // Notifications posted by handlers are appended and delivered in this same pass, up to the cap.
    for (size_t Index = 0; Index < self->_eventQueue._count; Index++)
    {
        if (DeliveredCount >= UI_SCREEN_MAX_EVENTS_PER_DISPATCH)
        {
            // Logged once per streak: handlers caught in a loop would otherwise log every update.
            IsOverflowing = true;
            if (!self->_isEventOverflowLogged && (self->_logger != NULL))
            {
                Error LogResult = Logger_LogWarningFormatted(self->_logger,
                    (const unsigned char*)u8"UIScreen: %" PRIu64 " notifications were dropped: handlers kept posting new "
                    u8"ones (more than %" PRIu64 " in one update). Not logged again until an update delivers its "
                    u8"notifications without dropping any.",
                    (uint64_t)(self->_eventQueue._count - Index), (uint64_t)UI_SCREEN_MAX_EVENTS_PER_DISPATCH);
                Error_Deconstruct(&LogResult);
            }
            self->_isEventOverflowLogged = true;
            break;
        }
        // Copied out: handlers may grow (move) the queue.
        UIEvent Event = *(UIEvent*)GenericBuffer_GetPointerToElement(&self->_eventQueue, Index);
        if (!IsDeliverable(self, &Event))
        {
            continue;
        }
        DeliveredCount++;
        Result = DeliverEvent(self, &Event);
        if (Result.Code != ErrorCode_Success)
        {
            break;
        }
    }
    // An update with nothing to deliver does not end the streak.
    if (!IsOverflowing && (DeliveredCount > 0U))
    {
        self->_isEventOverflowLogged = false;
    }
    GenericBuffer_Clear(&self->_eventQueue);
    self->_isDispatching = false;
    RemoveMarkedSubscriptions(self);
    return Result;
}

void UIScreen_InternalRemoveWidgetSubscriptions(UIScreen* self, uint64_t widgetId)
{
    for (size_t Index = 0; Index < self->_subscriptions._count; Index++)
    {
        Subscription* Entry = GetSubscriptionAt(self, Index);
        if (Entry->WidgetId == widgetId)
        {
            Entry->IsRemoved = true;
        }
    }
    if (!self->_isDispatching)
    {
        RemoveMarkedSubscriptions(self);
    }
}

void UIScreen_InternalReleaseEvents(UIScreen* self)
{
    Memory_Free(self->_eventQueue._data);
    Memory_Free(self->_subscriptions._data);
    Memory_Zero(&self->_eventQueue, sizeof(self->_eventQueue));
    Memory_Zero(&self->_subscriptions, sizeof(self->_subscriptions));
}


// Public functions.
Error UIScreen_Subscribe(UIScreen* self,
    uint64_t widgetId,
    uint32_t kindMask,
    UIEventHandler handler,
    const UserData* userData,
    uint64_t* outSubscriptionId)
{
    if ((self == NULL) || (handler == NULL) || (outSubscriptionId == NULL) || (kindMask == 0U))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "UIScreen_Subscribe: self, handler and outSubscriptionId must not be NULL and kindMask must not be 0.");
    }
    if ((widgetId != WIDGET_ID_INVALID) && (UIScreen_InternalResolve(self, widgetId) == NULL))
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_Subscribe: the widget is not alive.");
    }

    Subscription Entry =
    {
        .Id = self->_nextSubscriptionId,
        .WidgetId = widgetId,
        .KindMask = kindMask,
        .Handler = handler,
        .Data = (userData != NULL) ? *userData : UserData_CreateEmpty(),
        .IsRemoved = false
    };
    if (!GenericBuffer_AddLast(&self->_subscriptions, &Entry))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_Subscribe: the subscription could not be stored.");
    }
    *outSubscriptionId = Entry.Id;
    self->_nextSubscriptionId++;
    return Error_CreateSuccess();
}

Error UIScreen_Unsubscribe(UIScreen* self, uint64_t subscriptionId)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_Unsubscribe: self must not be NULL.");
    }
    for (size_t Index = 0; Index < self->_subscriptions._count; Index++)
    {
        Subscription* Entry = GetSubscriptionAt(self, Index);
        if ((Entry->Id == subscriptionId) && !Entry->IsRemoved)
        {
            Entry->IsRemoved = true;
            if (!self->_isDispatching)
            {
                RemoveMarkedSubscriptions(self);
            }
            return Error_CreateSuccess();
        }
    }
    return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_Unsubscribe: no such subscription.");
}

Error UIScreen_PostWidgetEvent(UIScreen* self, uint64_t widgetId, int32_t widgetEventId, const UIPropertyValue* value)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "UIScreen_PostWidgetEvent: self must not be NULL.");
    }
    Widget* Source = UIScreen_InternalResolve(self, widgetId);
    if (Source == NULL)
    {
        return Error_Construct2(ErrorCode_InvalidOperation, "UIScreen_PostWidgetEvent: the widget is not alive.");
    }

    UIEvent Event =
    {
        .Kind = UIEventKind_Widget,
        .WidgetId = widgetId,
        .WidgetTypeId = Widget_GetTypeId(Source),
        .IsSet = false,
        .WidgetEventId = widgetEventId,
        .HasValue = (value != NULL),
        .Value = (value != NULL) ? *value : UIPropertyValue_FromFloat(0.0f)
    };
    if (!UIScreen_InternalPostEvent(self, &Event))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "UIScreen_PostWidgetEvent: the notification queue could not grow.");
    }
    return Error_CreateSuccess();
}
