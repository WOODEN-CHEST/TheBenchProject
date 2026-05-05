#pragma once
#if defined(__linux__)
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#endif

#include "WRThread.h"
#include <stdatomic.h>

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error "WRThread is only supported on Linux and Windows."
#endif


// Types.
struct ThreadStruct
{
    ThreadFunc _entryFunction;
    void* _userdata;
    void* _result;
    atomic_bool _hasCompleted;
    bool _isJoinable;
    bool _isJoined;
    bool _tracksCompletion;

#if defined(__linux__)
    pthread_t _thread;
    bool _hasNativeThread;
#elif defined(_WIN32)
    HANDLE _handle;
    DWORD _threadId;
#endif
};

struct MutexStruct
{
#if defined(__linux__)
    pthread_mutex_t _nativeMutex;
#elif defined(_WIN32)
    CRITICAL_SECTION _nativeMutex;
#endif
};

struct ConditionVariableStruct
{
#if defined(__linux__)
    pthread_cond_t _nativeConditionVariable;
#elif defined(_WIN32)
    CONDITION_VARIABLE _nativeConditionVariable;
#endif
};

struct ReadWriteLockStruct
{
#if defined(__linux__)
    pthread_rwlock_t _nativeReadWriteLock;
#elif defined(_WIN32)
    SRWLOCK _nativeReadWriteLock;
#endif
};


// Functions.
Error Thread_PlatformStart(Thread* self);

Error Thread_PlatformJoin(Thread* self);

bool Thread_PlatformIsRunning(Thread* self);

Error Thread_PlatformDeconstruct(Thread* self);

Error Thread_PlatformGetCurrent(Thread* self);

Error Mutex_PlatformCreate(Mutex* self);

Error Mutex_PlatformLock(Mutex* self);

Error Mutex_PlatformTryLock(Mutex* self, bool* outAcquired);

Error Mutex_PlatformRelease(Mutex* self);

Error Mutex_PlatformDeconstruct(Mutex* self);

Error ConditionVariable_PlatformCreate(ConditionVariable* self);

Error ConditionVariable_PlatformWait(ConditionVariable* self, Mutex* mutex);

Error ConditionVariable_PlatformWaitTimeout(ConditionVariable* self,
    Mutex* mutex,
    size_t milliseconds,
    bool* outTimedOut);

Error ConditionVariable_PlatformSignal(ConditionVariable* self);

Error ConditionVariable_PlatformBroadcast(ConditionVariable* self);

Error ConditionVariable_PlatformDeconstruct(ConditionVariable* self);

Error ReadWriteLock_PlatformCreate(ReadWriteLock* self);

Error ReadWriteLock_PlatformLockRead(ReadWriteLock* self);

Error ReadWriteLock_PlatformLockWrite(ReadWriteLock* self);

Error ReadWriteLock_PlatformReleaseRead(ReadWriteLock* self);

Error ReadWriteLock_PlatformReleaseWrite(ReadWriteLock* self);

Error ReadWriteLock_PlatformDeconstruct(ReadWriteLock* self);

Error Thread_PlatformSleep(size_t milliseconds);
