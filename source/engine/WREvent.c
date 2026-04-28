#include "WREvent.h"


// Macros.
#define WREVENT_DEFAULT_INITIAL_CAPACITY ((size_t)0)


// Types.


// Fields.


// Static functions.
static Error WREvent_CreateIllegalArgumentError(const unsigned char* message)
{
    return Error_Construct1(ErrorCode_IllegalArgument, message);
}

static Error WREvent_CreateInvalidOperationError(const unsigned char* message)
{
    return Error_Construct1(ErrorCode_InvalidOperation, message);
}

static bool WREvent_IsSubscriberBufferValid(GenericBuffer* buffer)
{
    return ((buffer != NULL)
        && (buffer->_elementSize == sizeof(WREventSubscriber))
        && !GenericBuffer_IsReadOnly(buffer));
}

static size_t WREvent_FindInsertIndex(WREvent* self, WREventPriority priority)
{
    WREventSubscriber* Subscribers = NULL;
    size_t SubscriberCount = 0;

    SubscriberCount = self->_activeSubscribers->_count;
    Subscribers = GenericBuffer_GetPointerToFirst(self->_activeSubscribers);
    for (size_t Index = 0; Index < SubscriberCount; Index++)
    {
        if (Subscribers[Index]._priority < priority)
        {
            return Index;
        }
    }

    return SubscriberCount;
}

static size_t WREvent_FindSubscriberIndex(WREvent* self, WREventHandle handle)
{
    WREventSubscriber* Subscribers = NULL;
    size_t SubscriberCount = 0;

    SubscriberCount = self->_activeSubscribers->_count;
    Subscribers = GenericBuffer_GetPointerToFirst(self->_activeSubscribers);
    for (size_t Index = 0; Index < SubscriberCount; Index++)
    {
        if (Subscribers[Index]._handle == handle)
        {
            return Index;
        }
    }

    return GENERIC_BUFFER_INDEX_INVALID;
}

static Error WREvent_Initialize(WREvent* self,
    GenericBuffer* subscriberBuffer,
    GenericBuffer* subscriberSnapshotBuffer,
    bool areBuffersOwned)
{
    if (self == NULL)
    {
        return WREvent_CreateIllegalArgumentError(u8"The event instance must not be null.");
    }
    if (!WREvent_IsSubscriberBufferValid(subscriberBuffer))
    {
        return WREvent_CreateIllegalArgumentError(u8"The subscriber buffer must be writable and store WREventSubscriber values.");
    }
    if (!WREvent_IsSubscriberBufferValid(subscriberSnapshotBuffer))
    {
        return WREvent_CreateIllegalArgumentError(u8"The subscriber snapshot buffer must be writable and store WREventSubscriber values.");
    }
    if (subscriberBuffer == subscriberSnapshotBuffer)
    {
        return WREvent_CreateIllegalArgumentError(u8"The subscriber buffer and snapshot buffer must be different buffers.");
    }
    if (!GenericBuffer_Clear(subscriberBuffer))
    {
        return WREvent_CreateInvalidOperationError(u8"Failed to clear the subscriber buffer during event construction.");
    }
    if (!GenericBuffer_Clear(subscriberSnapshotBuffer))
    {
        return WREvent_CreateInvalidOperationError(u8"Failed to clear the subscriber snapshot buffer during event construction.");
    }

    self->_nextAvailableHandle = 1;
    self->_activeSubscribers = subscriberBuffer;
    self->_activeRaiseSubscriberSnapshot = subscriberSnapshotBuffer;
    self->_areBuffersOwned = areBuffersOwned;
    return Error_CreateSuccess();
}

static Error WREvent_CreateBufferTooSmallError(const unsigned char* message)
{
    return Error_Construct1(ErrorCode_BufferTooSmall, message);
}


// Public functions.
Error WREvent_Construct1(WREvent* self)
{
    if (self == NULL)
    {
        return WREvent_CreateIllegalArgumentError(u8"The event instance must not be null.");
    }

    GenericBuffer_AllocateVariable(&self->_selfSubscribers,
        WREVENT_DEFAULT_INITIAL_CAPACITY,
        sizeof(WREventSubscriber));
    GenericBuffer_AllocateVariable(&self->_selfRaiseSubscriberSnapshot,
        WREVENT_DEFAULT_INITIAL_CAPACITY,
        sizeof(WREventSubscriber));

    return WREvent_Initialize(self, &self->_selfSubscribers, &self->_selfRaiseSubscriberSnapshot, true);
}

Error WREvent_Construct2(WREvent* self, GenericBuffer* subscriberBuffer, GenericBuffer* subscriberSnapshotBuffer)
{
    return WREvent_Initialize(self, subscriberBuffer, subscriberSnapshotBuffer, false);
}

void WREvent_Deconstruct(WREvent* self)
{
    if (self == NULL)
    {
        return;
    }

    if (self->_areBuffersOwned)
    {
        Memory_Free(self->_selfSubscribers._data);
        Memory_Free(self->_selfRaiseSubscriberSnapshot._data);
    }

    self->_selfSubscribers = (GenericBuffer){ 0 };
    self->_selfRaiseSubscriberSnapshot = (GenericBuffer){ 0 };
    self->_activeSubscribers = NULL;
    self->_activeRaiseSubscriberSnapshot = NULL;
    self->_nextAvailableHandle = WREVENT_HANDLE_INVALID;
    self->_areBuffersOwned = false;
}

Error WREvent_Subscribe(WREvent* self,
    WREventHandlerFunction handler,
    WREventPriority priority,
    void* userData,
    WREventHandle* outHandle)
{
    WREventHandle Handle = WREVENT_HANDLE_INVALID;
    WREventSubscriber Subscriber = { 0 };
    size_t InsertIndex = 0;

    if (self == NULL)
    {
        return WREvent_CreateIllegalArgumentError(u8"The event instance must not be null.");
    }
    if (handler == NULL)
    {
        return WREvent_CreateIllegalArgumentError(u8"The event handler must not be null.");
    }
    if (outHandle == NULL)
    {
        return WREvent_CreateIllegalArgumentError(u8"The output handle pointer must not be null.");
    }
    if (self->_activeSubscribers == NULL)
    {
        return WREvent_CreateInvalidOperationError(u8"The event must be constructed before it can be subscribed to.");
    }

    Handle = self->_nextAvailableHandle;
    if (Handle == WREVENT_HANDLE_INVALID)
    {
        return WREvent_CreateInvalidOperationError(u8"The event has run out of subscriber handles.");
    }

    Subscriber = (WREventSubscriber)
    {
        ._handle = Handle,
        ._handler = handler,
        ._priority = priority,
        ._userData = userData,
    };
    InsertIndex = WREvent_FindInsertIndex(self, priority);
    if (!GenericBuffer_Insert(self->_activeSubscribers, &Subscriber, InsertIndex))
    {
        return WREvent_CreateBufferTooSmallError(u8"Failed to store the event subscriber.");
    }

    self->_nextAvailableHandle++;
    *outHandle = Handle;
    return Error_CreateSuccess();
}

Error WREvent_Unsubscribe(WREvent* self, WREventHandle handle)
{
    size_t SubscriberIndex = 0;

    if (self == NULL)
    {
        return WREvent_CreateIllegalArgumentError(u8"The event instance must not be null.");
    }
    if (handle == WREVENT_HANDLE_INVALID)
    {
        return WREvent_CreateIllegalArgumentError(u8"The subscriber handle must not be the invalid handle value.");
    }
    if (self->_activeSubscribers == NULL)
    {
        return WREvent_CreateInvalidOperationError(u8"The event must be constructed before it can unsubscribe handlers.");
    }

    SubscriberIndex = WREvent_FindSubscriberIndex(self, handle);
    if (SubscriberIndex == GENERIC_BUFFER_INDEX_INVALID)
    {
        return WREvent_CreateInvalidOperationError(u8"No subscriber with the provided handle exists on this event.");
    }
    if (!GenericBuffer_RemoveAt(self->_activeSubscribers, SubscriberIndex))
    {
        return WREvent_CreateInvalidOperationError(u8"Failed to remove the subscriber from the event.");
    }

    return Error_CreateSuccess();
}

Error WREvent_UnsubscribeAll(WREvent* self)
{
    if (self == NULL)
    {
        return WREvent_CreateIllegalArgumentError(u8"The event instance must not be null.");
    }
    if (self->_activeSubscribers == NULL)
    {
        return WREvent_CreateInvalidOperationError(u8"The event must be constructed before it can unsubscribe handlers.");
    }
    if (!GenericBuffer_Clear(self->_activeSubscribers))
    {
        return WREvent_CreateInvalidOperationError(u8"Failed to clear the event subscribers.");
    }

    return Error_CreateSuccess();
}

size_t WREvent_GetSubscriberCount(WREvent* self)
{
    if ((self == NULL) || (self->_activeSubscribers == NULL))
    {
        return 0;
    }

    return self->_activeSubscribers->_count;
}

Error WREvent_Raise(WREvent* self, void* eventArgs)
{
    WREventSubscriber* SnapshotSubscribers = NULL;
    size_t SnapshotCount = 0;

    if (self == NULL)
    {
        return WREvent_CreateIllegalArgumentError(u8"The event instance must not be null.");
    }
    if ((self->_activeSubscribers == NULL) || (self->_activeRaiseSubscriberSnapshot == NULL))
    {
        return WREvent_CreateInvalidOperationError(u8"The event must be constructed before it can be raised.");
    }
    if (!GenericBuffer_Clear(self->_activeRaiseSubscriberSnapshot))
    {
        return WREvent_CreateInvalidOperationError(u8"Failed to clear the subscriber snapshot buffer before raising the event.");
    }
    if (!GenericBuffer_AddLastRange(self->_activeRaiseSubscriberSnapshot,
        self->_activeSubscribers->_data,
        self->_activeSubscribers->_count))
    {
        return WREvent_CreateBufferTooSmallError(u8"Failed to create a snapshot of the event subscribers.");
    }

    SnapshotCount = self->_activeRaiseSubscriberSnapshot->_count;
    SnapshotSubscribers = GenericBuffer_GetPointerToFirst(self->_activeRaiseSubscriberSnapshot);
    for (size_t Index = 0; Index < SnapshotCount; Index++)
    {
        WREventArgs Args = (WREventArgs)
        {
            ._userData = SnapshotSubscribers[Index]._userData,
            ._eventArgs = eventArgs,
        };
        Error Result = SnapshotSubscribers[Index]._handler(&Args);

        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }
    }

    return Error_CreateSuccess();
}
