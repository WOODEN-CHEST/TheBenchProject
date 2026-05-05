#include "WRThreadInternal.h"


// Static functions.
static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Thread argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateMissingCallbackError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Thread callback argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateInvalidJoinError(void)
{
    return Error_Construct1(ErrorCode_InvalidOperation,
        u8"Only threads created through Thread_Create can be joined.");
}

static Error CreateAlreadyJoinedError(void)
{
    return Error_Construct1(ErrorCode_InvalidState,
        u8"The thread has already been joined.");
}

static void InitializeThreadCommon(Thread* self, ThreadFunc entryFunction, void* userdata)
{
    self->_entryFunction = entryFunction;
    self->_userdata = userdata;
    self->_result = NULL;
    atomic_init(&self->_hasCompleted, false);
    self->_isJoinable = false;
    self->_isJoined = false;
    self->_tracksCompletion = false;

#if defined(__linux__)
    self->_hasNativeThread = false;
#elif defined(_WIN32)
    self->_handle = NULL;
    self->_threadId = 0;
#endif
}


// Public functions.
Error Thread_Create(Thread** outThread, ThreadFunc entryFunction, void* userdata)
{
    Thread* ThreadValue = NULL;
    Error Result = Error_CreateSuccess();

    if (outThread == NULL)
    {
        return CreateNullArgumentError(u8"outThread");
    }
    if (entryFunction == NULL)
    {
        *outThread = NULL;
        return CreateMissingCallbackError(u8"entryFunction");
    }

    *outThread = NULL;
    ThreadValue = Memory_Allocate(sizeof(Thread));
    InitializeThreadCommon(ThreadValue, entryFunction, userdata);

    Result = Thread_PlatformStart(ThreadValue);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(ThreadValue);
        return Result;
    }

    *outThread = ThreadValue;
    return Error_CreateSuccess();
}

Error Thread_Join(Thread* self, void** outResult)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!self->_isJoinable)
    {
        if (outResult != NULL)
        {
            *outResult = NULL;
        }

        return CreateInvalidJoinError();
    }
    if (self->_isJoined)
    {
        if (outResult != NULL)
        {
            *outResult = self->_result;
        }

        return CreateAlreadyJoinedError();
    }

    Result = Thread_PlatformJoin(self);
    if (Result.Code != ErrorCode_Success)
    {
        if (outResult != NULL)
        {
            *outResult = NULL;
        }

        return Result;
    }

    self->_isJoined = true;
    if (outResult != NULL)
    {
        *outResult = self->_result;
    }

    return Error_CreateSuccess();
}

bool Thread_IsRunning(Thread* self)
{
    if (self == NULL)
    {
        return false;
    }
    if (self->_isJoined)
    {
        return false;
    }
    if (self->_tracksCompletion)
    {
        return !atomic_load(&self->_hasCompleted);
    }

    return Thread_PlatformIsRunning(self);
}

Error Thread_Deconstruct(Thread* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error Result = Thread_PlatformDeconstruct(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Free(self);
    return Error_CreateSuccess();
}

Error Thread_Sleep(size_t milliseconds)
{
    return Thread_PlatformSleep(milliseconds);
}

Error Thread_GetCurrent(Thread** outThread)
{
    Thread* ThreadValue = NULL;
    Error Result = Error_CreateSuccess();

    if (outThread == NULL)
    {
        return CreateNullArgumentError(u8"outThread");
    }

    *outThread = NULL;
    ThreadValue = Memory_Allocate(sizeof(Thread));
    InitializeThreadCommon(ThreadValue, NULL, NULL);
    Result = Thread_PlatformGetCurrent(ThreadValue);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(ThreadValue);
        return Result;
    }

    *outThread = ThreadValue;
    return Error_CreateSuccess();
}

Error Mutex_Create(Mutex** outMutex)
{
    Mutex* MutexValue = NULL;
    Error Result = Error_CreateSuccess();

    if (outMutex == NULL)
    {
        return CreateNullArgumentError(u8"outMutex");
    }

    *outMutex = NULL;
    MutexValue = Memory_Allocate(sizeof(Mutex));
    Result = Mutex_PlatformCreate(MutexValue);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(MutexValue);
        return Result;
    }

    *outMutex = MutexValue;
    return Error_CreateSuccess();
}

Error Mutex_Lock(Mutex* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Mutex_PlatformLock(self);
}

Error Mutex_TryLock(Mutex* mutex, bool* outAcquired)
{
    if (mutex == NULL)
    {
        return CreateNullArgumentError(u8"mutex");
    }
    if (outAcquired == NULL)
    {
        return CreateNullArgumentError(u8"outAcquired");
    }

    return Mutex_PlatformTryLock(mutex, outAcquired);
}

Error Mutex_Release(Mutex* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return Mutex_PlatformRelease(self);
}

Error Mutex_Deconstruct(Mutex* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error Result = Mutex_PlatformDeconstruct(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Free(self);
    return Error_CreateSuccess();
}

Error ConditionVariable_Create(ConditionVariable** outVariable)
{
    ConditionVariable* Variable = NULL;
    Error Result = Error_CreateSuccess();

    if (outVariable == NULL)
    {
        return CreateNullArgumentError(u8"outVariable");
    }

    *outVariable = NULL;
    Variable = Memory_Allocate(sizeof(ConditionVariable));
    Result = ConditionVariable_PlatformCreate(Variable);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Variable);
        return Result;
    }

    *outVariable = Variable;
    return Error_CreateSuccess();
}

Error ConditionVariable_Wait(ConditionVariable* self, Mutex* mutex)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (mutex == NULL)
    {
        return CreateNullArgumentError(u8"mutex");
    }

    return ConditionVariable_PlatformWait(self, mutex);
}

Error ConditionVariable_WaitTimeout(ConditionVariable* self, Mutex* mutex, size_t milliseconds, bool* outTimedOut)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (mutex == NULL)
    {
        return CreateNullArgumentError(u8"mutex");
    }
    if (outTimedOut == NULL)
    {
        return CreateNullArgumentError(u8"outTimedOut");
    }

    return ConditionVariable_PlatformWaitTimeout(self, mutex, milliseconds, outTimedOut);
}

Error ConditionVariable_Signal(ConditionVariable* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return ConditionVariable_PlatformSignal(self);
}

Error ConditionVariable_Broadcast(ConditionVariable* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return ConditionVariable_PlatformBroadcast(self);
}

Error ConditionVariable_Deconstruct(ConditionVariable* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error Result = ConditionVariable_PlatformDeconstruct(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Free(self);
    return Error_CreateSuccess();
}

Error ReadWriteLock_Create(ReadWriteLock** outLock)
{
    ReadWriteLock* Lock = NULL;
    Error Result = Error_CreateSuccess();

    if (outLock == NULL)
    {
        return CreateNullArgumentError(u8"outLock");
    }

    *outLock = NULL;
    Lock = Memory_Allocate(sizeof(ReadWriteLock));
    Result = ReadWriteLock_PlatformCreate(Lock);
    if (Result.Code != ErrorCode_Success)
    {
        Memory_Free(Lock);
        return Result;
    }

    *outLock = Lock;
    return Error_CreateSuccess();
}

Error ReadWriteLock_LockRead(ReadWriteLock* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return ReadWriteLock_PlatformLockRead(self);
}

Error ReadWriteLock_LockWrite(ReadWriteLock* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return ReadWriteLock_PlatformLockWrite(self);
}

Error ReadWriteLock_ReleaseRead(ReadWriteLock* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return ReadWriteLock_PlatformReleaseRead(self);
}

Error ReadWriteLock_ReleaseWrite(ReadWriteLock* self)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }

    return ReadWriteLock_PlatformReleaseWrite(self);
}

Error ReadWriteLock_Deconstruct(ReadWriteLock* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Error Result = ReadWriteLock_PlatformDeconstruct(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Free(self);
    return Error_CreateSuccess();
}
