#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRMemory.h"
#include "WRError.h"




// Types.
typedef void* (*ThreadFunc)(void* userdata);
typedef struct ThreadStruct Thread;
typedef struct MutexStruct Mutex;
typedef struct ConditionVariableStruct ConditionVariable;
typedef struct ReadWriteLockStruct ReadWriteLock;



// Functions.
Error Thread_Create(Thread** outThread, ThreadFunc entryFunction, void* userdata);

Error Thread_Join(Thread* self, void** outResult);

bool  Thread_IsRunning(Thread* self);

Error Thread_Deconstruct(Thread* self);

Error Thread_Sleep(size_t milliseconds);

Error Thread_GetCurrent(Thread** outThread);


Error Mutex_Create(Mutex** outMutex);

Error Mutex_Lock(Mutex* self);

Error Mutex_TryLock(Mutex* mutex, bool* outAcquired);

Error Mutex_Release(Mutex* self);

Error Mutex_Deconstruct(Mutex* self);


Error ConditionVariable_Create(ConditionVariable** outVariable);

Error ConditionVariable_Wait(ConditionVariable* self, Mutex* mutex);

Error ConditionVariable_WaitTimeout(ConditionVariable* self, Mutex* mutex, size_t milliseconds, bool* outTimedOut);

Error ConditionVariable_Signal(ConditionVariable* self);

Error ConditionVariable_Broadcast(ConditionVariable* self);

Error ConditionVariable_Deconstruct(ConditionVariable* self);


Error ReadWriteLock_Create(ReadWriteLock** outLock);

Error ReadWriteLock_LockRead(ReadWriteLock* self);

Error ReadWriteLock_LockWrite(ReadWriteLock* self);

Error ReadWriteLock_ReleaseRead(ReadWriteLock* self);

Error ReadWriteLock_ReleaseWrite(ReadWriteLock* self);

Error ReadWriteLock_Deconstruct(ReadWriteLock* self);
