#pragma once
#include "WRMemory.h"
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"


#define WREVENT_HANDLE_INVALID ((WREventHandle)0)
#define WREVENT_PRIORITY_DEFAULT ((WREventPriority)0)

// Types.
typedef uint64_t WREventHandle;
typedef int64_t WREventPriority;

typedef struct WREventArgsStruct
{
    void* _userData;
    void* _eventArgs;
} WREventArgs;

typedef Error (*WREventHandlerFunction)(WREventArgs* args);

typedef struct WREventSubscriberStruct
{
    uint64_t _handle;
    WREventHandlerFunction _handler;
    WREventPriority _priority;
    void* _userData;
} WREventSubscriber;

typedef struct WREventStruct 
{
    WREventHandle _nextAvailableHandle;
    GenericBuffer _selfSubscribers;
    GenericBuffer _selfRaiseSubscriberSnapshot;
    GenericBuffer* _activeSubscribers;
    GenericBuffer* _activeRaiseSubscriberSnapshot;
    bool _areBuffersOwned;
} WREvent;


// Functions.
Error WREvent_Construct1(WREvent* self);

/* Note: this constructor clears both passed in buffers before using them.  */
Error WREvent_Construct2(WREvent* self, GenericBuffer* subscriberBuffer, GenericBuffer* subscriberSnapshotBuffer);

void WREvent_Deconstruct(WREvent* self);

Error WREvent_Subscribe(WREvent* self,
    WREventHandlerFunction handler,
    WREventPriority priority,
    void* userData,
    WREventHandle* outHandle);

Error WREvent_Unsubscribe(WREvent* self, WREventHandle handle);

Error WREvent_UnsubscribeAll(WREvent* self);

size_t WREvent_GetSubscriberCount(WREvent* self);

Error WREvent_Raise(WREvent* self, void* eventArgs);


static inline void* WREventArgs_GetEventArgs(WREventArgs* args)
{
    return args->_eventArgs;
}

static inline void* WREventArgs_GetUserData(WREventArgs* args)
{
    return args->_userData;
}
