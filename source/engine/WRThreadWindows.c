#include "WRThreadInternal.h"

#if defined(_WIN32)

#include <limits.h>


// Macros.
#define WINDOWS_WAIT_INFINITE ((DWORD)0xFFFFFFFFUL)


// Types.


// Fields.


// Static functions.
static Error CreateWin32Error(const unsigned char* operationName, DWORD nativeError)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Windows thread operation \"%s\" failed with native error %lu.",
        operationName,
        (unsigned long)nativeError);
}

static Error CreateRangeError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Windows thread operation \"%s\" exceeded the supported range.",
        operationName);
}

static DWORD ConvertTimeout(size_t milliseconds, Error* outError)
{
    if (milliseconds > (size_t)DWORD_MAX)
    {
        *outError = CreateRangeError(u8"convert timeout");
        return 0;
    }

    *outError = Error_CreateSuccess();
    return (DWORD)milliseconds;
}

static DWORD WINAPI ThreadStartRoutine(LPVOID userdata)
{
    Thread* ThreadValue = userdata;
    void* Result = ThreadValue->_entryFunction(ThreadValue->_userdata);

    ThreadValue->_result = Result;
    atomic_store(&ThreadValue->_hasCompleted, true);
    return 0;
}


// Public functions.
Error Thread_PlatformStart(Thread* self)
{
    self->_handle = CreateThread(NULL, 0, &ThreadStartRoutine, self, 0, &self->_threadId);
    if (self->_handle == NULL)
    {
        return CreateWin32Error(u8"CreateThread", GetLastError());
    }

    self->_isJoinable = true;
    self->_tracksCompletion = true;
    return Error_CreateSuccess();
}

Error Thread_PlatformJoin(Thread* self)
{
    DWORD WaitResult = WaitForSingleObject(self->_handle, WINDOWS_WAIT_INFINITE);

    if (WaitResult != WAIT_OBJECT_0)
    {
        return CreateWin32Error(u8"WaitForSingleObject", GetLastError());
    }
    if (!CloseHandle(self->_handle))
    {
        return CreateWin32Error(u8"CloseHandle", GetLastError());
    }

    self->_handle = NULL;
    return Error_CreateSuccess();
}

bool Thread_PlatformIsRunning(Thread* self)
{
    DWORD ExitCode = 0;

    if (self->_handle == NULL)
    {
        return false;
    }
    if (!GetExitCodeThread(self->_handle, &ExitCode))
    {
        return false;
    }

    return ExitCode == STILL_ACTIVE;
}

Error Thread_PlatformDeconstruct(Thread* self)
{
    if (self->_handle != NULL)
    {
        if (!CloseHandle(self->_handle))
        {
            return CreateWin32Error(u8"CloseHandle", GetLastError());
        }

        self->_handle = NULL;
    }

    return Error_CreateSuccess();
}

Error Thread_PlatformGetCurrent(Thread* self)
{
    HANDLE ProcessHandle = GetCurrentProcess();
    HANDLE CurrentThreadHandle = GetCurrentThread();

    if (!DuplicateHandle(ProcessHandle,
        CurrentThreadHandle,
        ProcessHandle,
        &self->_handle,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS))
    {
        return CreateWin32Error(u8"DuplicateHandle", GetLastError());
    }

    self->_threadId = GetCurrentThreadId();
    return Error_CreateSuccess();
}

Error Mutex_PlatformCreate(Mutex* self)
{
    InitializeCriticalSection(&self->_nativeMutex);
    return Error_CreateSuccess();
}

Error Mutex_PlatformLock(Mutex* self)
{
    EnterCriticalSection(&self->_nativeMutex);
    return Error_CreateSuccess();
}

Error Mutex_PlatformTryLock(Mutex* self, bool* outAcquired)
{
    *outAcquired = TryEnterCriticalSection(&self->_nativeMutex) != 0;
    return Error_CreateSuccess();
}

Error Mutex_PlatformRelease(Mutex* self)
{
    LeaveCriticalSection(&self->_nativeMutex);
    return Error_CreateSuccess();
}

Error Mutex_PlatformDeconstruct(Mutex* self)
{
    DeleteCriticalSection(&self->_nativeMutex);
    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformCreate(ConditionVariable* self)
{
    InitializeConditionVariable(&self->_nativeConditionVariable);
    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformWait(ConditionVariable* self, Mutex* mutex)
{
    if (!SleepConditionVariableCS(&self->_nativeConditionVariable, &mutex->_nativeMutex, WINDOWS_WAIT_INFINITE))
    {
        return CreateWin32Error(u8"SleepConditionVariableCS", GetLastError());
    }

    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformWaitTimeout(ConditionVariable* self,
    Mutex* mutex,
    size_t milliseconds,
    bool* outTimedOut)
{
    Error Result = Error_CreateSuccess();
    DWORD Timeout = 0;

    *outTimedOut = false;
    Timeout = ConvertTimeout(milliseconds, &Result);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (!SleepConditionVariableCS(&self->_nativeConditionVariable, &mutex->_nativeMutex, Timeout))
    {
        DWORD NativeError = GetLastError();

        if (NativeError == ERROR_TIMEOUT)
        {
            *outTimedOut = true;
            return Error_CreateSuccess();
        }

        return CreateWin32Error(u8"SleepConditionVariableCS", NativeError);
    }

    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformSignal(ConditionVariable* self)
{
    WakeConditionVariable(&self->_nativeConditionVariable);
    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformBroadcast(ConditionVariable* self)
{
    WakeAllConditionVariable(&self->_nativeConditionVariable);
    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformDeconstruct(ConditionVariable* self)
{
    (void)self;
    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformCreate(ReadWriteLock* self)
{
    InitializeSRWLock(&self->_nativeReadWriteLock);
    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformLockRead(ReadWriteLock* self)
{
    AcquireSRWLockShared(&self->_nativeReadWriteLock);
    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformLockWrite(ReadWriteLock* self)
{
    AcquireSRWLockExclusive(&self->_nativeReadWriteLock);
    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformReleaseRead(ReadWriteLock* self)
{
    ReleaseSRWLockShared(&self->_nativeReadWriteLock);
    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformReleaseWrite(ReadWriteLock* self)
{
    ReleaseSRWLockExclusive(&self->_nativeReadWriteLock);
    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformDeconstruct(ReadWriteLock* self)
{
    (void)self;
    return Error_CreateSuccess();
}

Error Thread_PlatformSleep(size_t milliseconds)
{
    Error Result = Error_CreateSuccess();
    DWORD Timeout = ConvertTimeout(milliseconds, &Result);

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Sleep(Timeout);
    return Error_CreateSuccess();
}

#endif
