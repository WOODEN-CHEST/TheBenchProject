#include "WRThreadInternal.h"

#if defined(__linux__)

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <time.h>


// Macros.
#define THREAD_SLEEP_CHUNK_MILLISECONDS ((size_t)86400000)
#define NANOSECONDS_PER_MILLISECOND (1000000L)
#define NANOSECONDS_PER_SECOND (1000000000L)


// Types.


// Fields.


// Static functions.
static Error CreatePThreadError(const unsigned char* operationName, int nativeError)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Linux thread operation \"%s\" failed with native error %d.",
        operationName,
        nativeError);
}

static Error CreateRangeError(const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Linux thread operation \"%s\" exceeded the supported range.",
        operationName);
}

static void* ThreadStartRoutine(void* userdata)
{
    Thread* ThreadValue = userdata;
    void* Result = ThreadValue->_entryFunction(ThreadValue->_userdata);

    ThreadValue->_result = Result;
    atomic_store(&ThreadValue->_hasCompleted, true);
    return Result;
}

static size_t GetMinimumSize(size_t leftValue, size_t rightValue)
{
    return (leftValue < rightValue) ? leftValue : rightValue;
}

static Error BuildAbsoluteTimeout(size_t milliseconds, struct timespec* outTimeout)
{
    struct timespec CurrentTime;
    int ClockResult = 0;
    int64_t AbsoluteSeconds = 0;
    time_t ConvertedSeconds = 0;
    long AdditionalNanoseconds = 0;

    if (outTimeout == NULL)
    {
        return Error_Construct1(ErrorCode_IllegalArgument,
            u8"Linux thread timeout output must not be null.");
    }

    ClockResult = clock_gettime(CLOCK_REALTIME, &CurrentTime);
    if (ClockResult != 0)
    {
        return CreatePThreadError(u8"clock_gettime", errno);
    }
    if (milliseconds > (size_t)INT64_MAX)
    {
        return CreateRangeError(u8"build absolute timeout");
    }

    AbsoluteSeconds = (int64_t)CurrentTime.tv_sec + (int64_t)(milliseconds / 1000U);
    ConvertedSeconds = (time_t)AbsoluteSeconds;
    if ((int64_t)ConvertedSeconds != AbsoluteSeconds)
    {
        return CreateRangeError(u8"build absolute timeout");
    }

    AdditionalNanoseconds = (long)((milliseconds % 1000U) * (size_t)NANOSECONDS_PER_MILLISECOND);
    outTimeout->tv_sec = ConvertedSeconds;
    outTimeout->tv_nsec = CurrentTime.tv_nsec + AdditionalNanoseconds;
    if (outTimeout->tv_nsec >= NANOSECONDS_PER_SECOND)
    {
        outTimeout->tv_nsec -= NANOSECONDS_PER_SECOND;
        outTimeout->tv_sec += 1;
    }

    return Error_CreateSuccess();
}


// Public functions.
Error Thread_PlatformStart(Thread* self)
{
    int NativeResult = 0;

    NativeResult = pthread_create(&self->_thread, NULL, &ThreadStartRoutine, self);
    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_create", NativeResult);
    }

    self->_isJoinable = true;
    self->_tracksCompletion = true;
    self->_hasNativeThread = true;
    return Error_CreateSuccess();
}

Error Thread_PlatformJoin(Thread* self)
{
    int NativeResult = 0;

    NativeResult = pthread_join(self->_thread, NULL);
    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_join", NativeResult);
    }

    self->_hasNativeThread = false;
    return Error_CreateSuccess();
}

bool Thread_PlatformIsRunning(Thread* self)
{
    int NativeResult = 0;

    if (!self->_hasNativeThread)
    {
        return false;
    }

    NativeResult = pthread_kill(self->_thread, 0);
    return NativeResult == 0;
}

Error Thread_PlatformDeconstruct(Thread* self)
{
    int NativeResult = 0;

    if (self->_isJoinable && !self->_isJoined && self->_hasNativeThread)
    {
        NativeResult = pthread_detach(self->_thread);
        if (NativeResult != 0)
        {
            return CreatePThreadError(u8"pthread_detach", NativeResult);
        }

        self->_hasNativeThread = false;
    }

    return Error_CreateSuccess();
}

Error Thread_PlatformGetCurrent(Thread* self)
{
    self->_thread = pthread_self();
    self->_hasNativeThread = true;
    return Error_CreateSuccess();
}

Error Mutex_PlatformCreate(Mutex* self)
{
    int NativeResult = pthread_mutex_init(&self->_nativeMutex, NULL);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_mutex_init", NativeResult);
    }

    return Error_CreateSuccess();
}

Error Mutex_PlatformLock(Mutex* self)
{
    int NativeResult = pthread_mutex_lock(&self->_nativeMutex);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_mutex_lock", NativeResult);
    }

    return Error_CreateSuccess();
}

Error Mutex_PlatformTryLock(Mutex* self, bool* outAcquired)
{
    int NativeResult = 0;

    *outAcquired = false;
    NativeResult = pthread_mutex_trylock(&self->_nativeMutex);
    if (NativeResult == 0)
    {
        *outAcquired = true;
        return Error_CreateSuccess();
    }
    if (NativeResult == EBUSY)
    {
        return Error_CreateSuccess();
    }

    return CreatePThreadError(u8"pthread_mutex_trylock", NativeResult);
}

Error Mutex_PlatformRelease(Mutex* self)
{
    int NativeResult = pthread_mutex_unlock(&self->_nativeMutex);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_mutex_unlock", NativeResult);
    }

    return Error_CreateSuccess();
}

Error Mutex_PlatformDeconstruct(Mutex* self)
{
    int NativeResult = pthread_mutex_destroy(&self->_nativeMutex);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_mutex_destroy", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformCreate(ConditionVariable* self)
{
    int NativeResult = pthread_cond_init(&self->_nativeConditionVariable, NULL);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_cond_init", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformWait(ConditionVariable* self, Mutex* mutex)
{
    int NativeResult = pthread_cond_wait(&self->_nativeConditionVariable, &mutex->_nativeMutex);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_cond_wait", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformWaitTimeout(ConditionVariable* self,
    Mutex* mutex,
    size_t milliseconds,
    bool* outTimedOut)
{
    struct timespec Timeout;
    Error Result = Error_CreateSuccess();
    int NativeResult = 0;

    *outTimedOut = false;
    Result = BuildAbsoluteTimeout(milliseconds, &Timeout);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    NativeResult = pthread_cond_timedwait(&self->_nativeConditionVariable, &mutex->_nativeMutex, &Timeout);
    if (NativeResult == ETIMEDOUT)
    {
        *outTimedOut = true;
        return Error_CreateSuccess();
    }
    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_cond_timedwait", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformSignal(ConditionVariable* self)
{
    int NativeResult = pthread_cond_signal(&self->_nativeConditionVariable);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_cond_signal", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformBroadcast(ConditionVariable* self)
{
    int NativeResult = pthread_cond_broadcast(&self->_nativeConditionVariable);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_cond_broadcast", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ConditionVariable_PlatformDeconstruct(ConditionVariable* self)
{
    int NativeResult = pthread_cond_destroy(&self->_nativeConditionVariable);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_cond_destroy", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformCreate(ReadWriteLock* self)
{
    int NativeResult = pthread_rwlock_init(&self->_nativeReadWriteLock, NULL);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_rwlock_init", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformLockRead(ReadWriteLock* self)
{
    int NativeResult = pthread_rwlock_rdlock(&self->_nativeReadWriteLock);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_rwlock_rdlock", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformLockWrite(ReadWriteLock* self)
{
    int NativeResult = pthread_rwlock_wrlock(&self->_nativeReadWriteLock);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_rwlock_wrlock", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformReleaseRead(ReadWriteLock* self)
{
    int NativeResult = pthread_rwlock_unlock(&self->_nativeReadWriteLock);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_rwlock_unlock", NativeResult);
    }

    return Error_CreateSuccess();
}

Error ReadWriteLock_PlatformReleaseWrite(ReadWriteLock* self)
{
    return ReadWriteLock_PlatformReleaseRead(self);
}

Error ReadWriteLock_PlatformDeconstruct(ReadWriteLock* self)
{
    int NativeResult = pthread_rwlock_destroy(&self->_nativeReadWriteLock);

    if (NativeResult != 0)
    {
        return CreatePThreadError(u8"pthread_rwlock_destroy", NativeResult);
    }

    return Error_CreateSuccess();
}

Error Thread_PlatformSleep(size_t milliseconds)
{
    size_t RemainingMilliseconds = milliseconds;

    while (RemainingMilliseconds > 0)
    {
        struct timespec Request;
        struct timespec RemainingTime;
        size_t SleepChunkMilliseconds = GetMinimumSize(RemainingMilliseconds, THREAD_SLEEP_CHUNK_MILLISECONDS);
        int NativeResult = 0;

        Request.tv_sec = (time_t)(SleepChunkMilliseconds / 1000U);
        Request.tv_nsec = (long)((SleepChunkMilliseconds % 1000U) * (size_t)NANOSECONDS_PER_MILLISECOND);
        NativeResult = nanosleep(&Request, &RemainingTime);
        if (NativeResult == 0)
        {
            RemainingMilliseconds -= SleepChunkMilliseconds;
            continue;
        }
        if (errno != EINTR)
        {
            return CreatePThreadError(u8"nanosleep", errno);
        }

        RemainingMilliseconds = (size_t)RemainingTime.tv_sec * 1000U;
        RemainingMilliseconds += (size_t)(RemainingTime.tv_nsec / NANOSECONDS_PER_MILLISECOND);
    }

    return Error_CreateSuccess();
}

#endif
