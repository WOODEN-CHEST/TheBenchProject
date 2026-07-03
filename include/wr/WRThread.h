#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRMemory.h"
#include "WRError.h"
#include "WRUserData.h"




// Types.
/**
 * @brief Entry point signature for a thread created with Thread_Create.
 *
 * The function is invoked once on the newly spawned thread. It receives a pointer to the thread's
 * own copy of the UserData supplied at creation (see Thread_Create); the pointee is stable for the
 * whole lifetime of the thread, so the function may read it freely. The returned pointer becomes the
 * thread's result and is later handed back through the out-parameter of Thread_Join. The framework
 * stores the returned pointer verbatim and never dereferences or frees it; any pointee it refers to
 * must be kept alive by the caller until it has been retrieved through Thread_Join.
 * @param userdata Non-NULL pointer to the thread's stored UserData copy. Read-only to the callee.
 * @returns An opaque result pointer surfaced through Thread_Join. May be NULL.
 */
typedef void* (*ThreadFunc)(const UserData* userdata);

/**
 * @brief Opaque handle to a single thread of execution and its associated state.
 *
 * Created either by Thread_Create (a freshly spawned, joinable thread) or by Thread_GetCurrent (a
 * non-joinable wrapper around the calling thread). In both cases the object is heap-allocated and
 * owned by the caller, who must eventually release it with Thread_Deconstruct. The concrete layout
 * is platform-specific and not exposed here.
 */
typedef struct ThreadStruct Thread;

/**
 * @brief Opaque mutual-exclusion lock used to serialize access to shared state.
 *
 * Created with Mutex_Create and released with Mutex_Deconstruct. A given mutex must be locked and
 * released by user code in balanced pairs. The recursive-locking behavior (whether the owning thread
 * may relock a mutex it already holds) is platform-dependent and must not be relied upon for portable
 * code. The concrete layout is platform-specific and not exposed here.
 */
typedef struct MutexStruct Mutex;

/**
 * @brief Opaque condition variable used to block threads until a predicate becomes true.
 *
 * Created with ConditionVariable_Create and released with ConditionVariable_Deconstruct. A condition
 * variable is always used together with a Mutex that protects the associated predicate: a waiter locks
 * the mutex, re-checks the predicate in a loop, and waits while it is false; a notifier changes the
 * predicate (typically under the same mutex) and then signals or broadcasts. The concrete layout is
 * platform-specific and not exposed here.
 */
typedef struct ConditionVariableStruct ConditionVariable;

/**
 * @brief Opaque reader-writer lock allowing many concurrent readers or one exclusive writer.
 *
 * Created with ReadWriteLock_Create and released with ReadWriteLock_Deconstruct. Read locks may be
 * held simultaneously by multiple threads; a write lock is exclusive of all other readers and writers.
 * Each acquired lock must be released with the matching release call for its mode. The concrete layout
 * is platform-specific and not exposed here.
 */
typedef struct ReadWriteLockStruct ReadWriteLock;



// Functions.
/**
 * @brief Spawns a new joinable thread that runs the given entry function.
 *
 * Allocates a Thread, copies the supplied UserData by value into it, and starts a native thread that
 * immediately invokes entryFunction with a pointer to that stored copy. On success the new thread is
 * joinable (see Thread_Join) and *outThread receives the owning handle, which must later be released
 * with Thread_Deconstruct. On any failure *outThread is set to NULL and no thread is left running.
 * @param outThread [out] Receives the newly created Thread handle on success, or NULL on failure.
 *        Must not be NULL.
 * @param entryFunction The function the new thread will execute. Must not be NULL.
 * @param userdata Pointer to a UserData value to copy into the thread and expose to entryFunction.
 *        May be NULL, in which case an empty (all-zero) UserData is used.
 * @returns Success once the thread has been started. ErrorCode_IllegalArgument if outThread or
 *          entryFunction is NULL; ErrorCode_InvalidOperation if the underlying native thread could not
 *          be created.
 * @note The pointer returned by entryFunction is retained only as the thread's result; retrieve it via
 *       Thread_Join before the referenced storage is freed.
 */
Error Thread_Create(Thread** outThread, ThreadFunc entryFunction, const UserData* userdata);

/**
 * @brief Blocks until the thread finishes and retrieves its result.
 *
 * Waits for the thread spawned by Thread_Create to return from its entry function, then writes the
 * thread's result (the pointer the entry function returned) to *outResult. A thread may be joined at
 * most once. This call does not free the Thread object; call Thread_Deconstruct afterwards.
 * @param self The thread to join. Must not be NULL and must have been created with Thread_Create.
 * @param outResult [out] Receives the thread's result pointer. May be NULL if the result is not
 *        wanted. On the error paths it is set to NULL, except on the already-joined error where it
 *        receives the previously stored result.
 * @returns Success after the thread has been joined and its result written. ErrorCode_IllegalArgument
 *          if self is NULL; ErrorCode_InvalidOperation if self was not created by Thread_Create (e.g.
 *          obtained from Thread_GetCurrent and therefore not joinable); ErrorCode_InvalidState if the
 *          thread has already been joined; ErrorCode_InvalidOperation if the native join call fails.
 */
Error Thread_Join(Thread* self, void** outResult);

/**
 * @brief Reports whether the thread is still executing.
 *
 * For threads created by Thread_Create the result reflects whether the entry function has completed:
 * it returns false once the function has returned (even before the thread is joined) and after the
 * thread has been joined. For wrappers from Thread_GetCurrent it queries the underlying native thread.
 * @param self The thread to query. May be NULL, in which case false is returned.
 * @returns true if the thread is considered to still be running, false otherwise (including when self
 *          is NULL or has already been joined).
 * @note This is an instantaneous, advisory check; the result may already be stale by the time it is
 *       observed. It does not block.
 */
bool  Thread_IsRunning(Thread* self);

/**
 * @brief Releases a Thread object and its native resources.
 *
 * Frees the storage owned by the Thread and any associated native handle. For a joinable thread that
 * has not been joined, the underlying native thread is detached (Linux) or its handle is closed
 * (Windows) so it can finish and clean itself up independently; this call does NOT wait for the thread
 * to finish. To obtain the thread's result, call Thread_Join before deconstructing. After this call
 * the handle must not be used again.
 * @param self The thread to release. May be NULL, in which case this is a no-op that returns success.
 * @returns Success once the object has been released. May return ErrorCode_InvalidOperation if the
 *          native detach/handle-close operation fails, in which case the object is not freed.
 */
Error Thread_Deconstruct(Thread* self);

/**
 * @brief Blocks the calling thread for at least the given number of milliseconds.
 *
 * Suspends the current thread for approximately the requested duration. A value of 0 returns
 * essentially immediately. The implementation accounts for early wakeups so the full duration elapses.
 * @param milliseconds The minimum time to sleep, in milliseconds.
 * @returns Success after the requested time has elapsed. ErrorCode_ArgumentOutOfRange if the duration
 *          exceeds the range the platform timer accepts; ErrorCode_InvalidOperation if the underlying
 *          native sleep call fails.
 */
Error Thread_Sleep(size_t milliseconds);

/**
 * @brief Creates a Thread object representing the calling thread.
 *
 * Allocates a Thread that wraps a handle to the thread that is currently executing. The returned
 * object is NOT joinable: passing it to Thread_Join fails with ErrorCode_InvalidOperation. It is
 * intended for identifying or inspecting the current thread and must be released with
 * Thread_Deconstruct.
 * @param outThread [out] Receives the new Thread handle on success, or NULL on failure. Must not be
 *        NULL.
 * @returns Success with *outThread populated. ErrorCode_IllegalArgument if outThread is NULL;
 *          ErrorCode_InvalidOperation if the underlying native handle could not be obtained.
 */
Error Thread_GetCurrent(Thread** outThread);


/**
 * @brief Creates an initialized mutex ready to be locked.
 *
 * Allocates and initializes a Mutex in the unlocked state. The result is owned by the caller and must
 * be released with Mutex_Deconstruct, which requires that the mutex is not held by any thread at the
 * time.
 * @param outMutex [out] Receives the new Mutex handle on success, or NULL on failure. Must not be NULL.
 * @returns Success with *outMutex populated. ErrorCode_IllegalArgument if outMutex is NULL;
 *          ErrorCode_InvalidOperation if the underlying native mutex could not be initialized.
 */
Error Mutex_Create(Mutex** outMutex);

/**
 * @brief Acquires the mutex, blocking until it is available.
 *
 * Blocks the calling thread until the mutex can be locked, then takes ownership of it. The lock must
 * later be released with Mutex_Release by the thread that holds it.
 * @param self The mutex to lock. Must not be NULL.
 * @returns Success once the lock is held. ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the underlying native lock operation fails.
 * @note Whether re-locking an already-held mutex from the same thread is allowed or deadlocks is
 *       platform-dependent; do not rely on recursive locking for portable code.
 */
Error Mutex_Lock(Mutex* self);

/**
 * @brief Attempts to acquire the mutex without blocking.
 *
 * Tries to lock the mutex and reports whether ownership was obtained. If it returns that the lock was
 * acquired, the caller owns the mutex and must release it with Mutex_Release; if not, no lock is held
 * and nothing needs to be released.
 * @param mutex The mutex to try to lock. Must not be NULL.
 * @param outAcquired [out] Set to true if the lock was acquired, false if the mutex was already held
 *        by someone else. Must not be NULL.
 * @returns Success whether or not the lock was acquired (the outcome is reported via outAcquired).
 *          ErrorCode_IllegalArgument if mutex or outAcquired is NULL; ErrorCode_InvalidOperation if
 *          the underlying native try-lock fails for a reason other than the mutex being busy.
 */
Error Mutex_TryLock(Mutex* mutex, bool* outAcquired);

/**
 * @brief Releases the mutex previously acquired by the calling thread.
 *
 * Unlocks a mutex that the calling thread currently holds, allowing another thread to acquire it.
 * @param self The mutex to release. Must not be NULL and must be held by the calling thread.
 * @returns Success once the lock is released. ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the underlying native unlock operation fails.
 */
Error Mutex_Release(Mutex* self);

/**
 * @brief Destroys a mutex and frees its resources.
 *
 * Releases the native mutex and frees the Mutex object. The mutex must be unlocked (not held by any
 * thread) when this is called. After this call the handle must not be used again.
 * @param self The mutex to destroy. May be NULL, in which case this is a no-op that returns success.
 * @returns Success once the object has been released. May return ErrorCode_InvalidOperation if the
 *          native destroy operation fails, in which case the object is not freed.
 */
Error Mutex_Deconstruct(Mutex* self);


/**
 * @brief Creates an initialized condition variable.
 *
 * Allocates and initializes a ConditionVariable. The result is owned by the caller and must be
 * released with ConditionVariable_Deconstruct once no thread is waiting on it.
 * @param outVariable [out] Receives the new ConditionVariable handle on success, or NULL on failure.
 *        Must not be NULL.
 * @returns Success with *outVariable populated. ErrorCode_IllegalArgument if outVariable is NULL;
 *          ErrorCode_InvalidOperation if the underlying native condition variable could not be
 *          initialized.
 */
Error ConditionVariable_Create(ConditionVariable** outVariable);

/**
 * @brief Atomically releases the mutex and blocks until the condition variable is notified.
 *
 * The calling thread must hold the given mutex. This call atomically releases that mutex and suspends
 * the thread until another thread signals or broadcasts the condition variable; before returning it
 * re-acquires the mutex, so the caller again owns it on return. Spurious wakeups are possible, so
 * callers must re-test their predicate in a loop and wait again if it is still not satisfied.
 * @param self The condition variable to wait on. Must not be NULL.
 * @param mutex The mutex that is currently held by the caller and guards the predicate. Must not be
 *        NULL.
 * @returns Success after the thread has been woken and the mutex re-acquired. ErrorCode_IllegalArgument
 *          if self or mutex is NULL; ErrorCode_InvalidOperation if the underlying native wait fails.
 */
Error ConditionVariable_Wait(ConditionVariable* self, Mutex* mutex);

/**
 * @brief Like ConditionVariable_Wait but gives up after a timeout.
 *
 * Behaves as ConditionVariable_Wait, atomically releasing the held mutex and re-acquiring it before
 * return, except that it stops waiting once the timeout elapses. Whether it returns because of a
 * notification or because of the timeout is reported through outTimedOut. Spurious wakeups are
 * possible, so callers must re-test their predicate.
 * @param self The condition variable to wait on. Must not be NULL.
 * @param mutex The mutex that is currently held by the caller and guards the predicate. Must not be
 *        NULL.
 * @param milliseconds The maximum time to wait before timing out, in milliseconds.
 * @param outTimedOut [out] Set to true if the wait ended because the timeout elapsed, false if it was
 *        woken by a notification. Must not be NULL.
 * @returns Success after the wait completes (whether woken or timed out) with the mutex re-acquired.
 *          ErrorCode_IllegalArgument if self, mutex, or outTimedOut is NULL; ErrorCode_ArgumentOutOfRange
 *          if the timeout exceeds the range the platform accepts; ErrorCode_InvalidOperation if the
 *          underlying native wait fails.
 */
Error ConditionVariable_WaitTimeout(ConditionVariable* self, Mutex* mutex, size_t milliseconds, bool* outTimedOut);

/**
 * @brief Wakes one thread waiting on the condition variable.
 *
 * Unblocks at least one thread currently waiting on the condition variable, if any. If no thread is
 * waiting the call has no effect. The associated predicate should be updated (typically under the
 * guarding mutex) before signaling.
 * @param self The condition variable to signal. Must not be NULL.
 * @returns Success. ErrorCode_IllegalArgument if self is NULL; ErrorCode_InvalidOperation if the
 *          underlying native signal operation fails.
 */
Error ConditionVariable_Signal(ConditionVariable* self);

/**
 * @brief Wakes all threads waiting on the condition variable.
 *
 * Unblocks every thread currently waiting on the condition variable. If no thread is waiting the call
 * has no effect. The associated predicate should be updated (typically under the guarding mutex)
 * before broadcasting.
 * @param self The condition variable to broadcast to. Must not be NULL.
 * @returns Success. ErrorCode_IllegalArgument if self is NULL; ErrorCode_InvalidOperation if the
 *          underlying native broadcast operation fails.
 */
Error ConditionVariable_Broadcast(ConditionVariable* self);

/**
 * @brief Destroys a condition variable and frees its resources.
 *
 * Releases the native condition variable and frees the object. No thread may be waiting on it when
 * this is called. After this call the handle must not be used again.
 * @param self The condition variable to destroy. May be NULL, in which case this is a no-op that
 *        returns success.
 * @returns Success once the object has been released. May return ErrorCode_InvalidOperation if the
 *          native destroy operation fails, in which case the object is not freed.
 */
Error ConditionVariable_Deconstruct(ConditionVariable* self);


/**
 * @brief Creates an initialized reader-writer lock.
 *
 * Allocates and initializes a ReadWriteLock in the unlocked state. The result is owned by the caller
 * and must be released with ReadWriteLock_Deconstruct once no thread holds it.
 * @param outLock [out] Receives the new ReadWriteLock handle on success, or NULL on failure. Must not
 *        be NULL.
 * @returns Success with *outLock populated. ErrorCode_IllegalArgument if outLock is NULL;
 *          ErrorCode_InvalidOperation if the underlying native lock could not be initialized.
 */
Error ReadWriteLock_Create(ReadWriteLock** outLock);

/**
 * @brief Acquires the lock for shared (read) access, blocking until available.
 *
 * Blocks until read access can be granted, then takes a shared lock. Multiple threads may hold the
 * read lock at the same time, but it blocks while a writer holds or is being granted the lock. Must be
 * released with ReadWriteLock_ReleaseRead.
 * @param self The lock to acquire for reading. Must not be NULL.
 * @returns Success once the read lock is held. ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the underlying native lock operation fails.
 */
Error ReadWriteLock_LockRead(ReadWriteLock* self);

/**
 * @brief Acquires the lock for exclusive (write) access, blocking until available.
 *
 * Blocks until no other thread holds the lock in any mode, then takes an exclusive lock that excludes
 * all other readers and writers. Must be released with ReadWriteLock_ReleaseWrite.
 * @param self The lock to acquire for writing. Must not be NULL.
 * @returns Success once the write lock is held. ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the underlying native lock operation fails.
 */
Error ReadWriteLock_LockWrite(ReadWriteLock* self);

/**
 * @brief Releases a shared (read) lock held by the calling thread.
 *
 * Drops one shared lock previously acquired with ReadWriteLock_LockRead. Must be paired with a prior
 * read acquisition by the same thread.
 * @param self The lock to release. Must not be NULL.
 * @returns Success once the read lock is released. ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the underlying native unlock operation fails.
 */
Error ReadWriteLock_ReleaseRead(ReadWriteLock* self);

/**
 * @brief Releases an exclusive (write) lock held by the calling thread.
 *
 * Drops the exclusive lock previously acquired with ReadWriteLock_LockWrite. Must be paired with a
 * prior write acquisition by the same thread.
 * @param self The lock to release. Must not be NULL.
 * @returns Success once the write lock is released. ErrorCode_IllegalArgument if self is NULL;
 *          ErrorCode_InvalidOperation if the underlying native unlock operation fails.
 */
Error ReadWriteLock_ReleaseWrite(ReadWriteLock* self);

/**
 * @brief Destroys a reader-writer lock and frees its resources.
 *
 * Releases the native lock and frees the object. No thread may hold the lock in any mode when this is
 * called. After this call the handle must not be used again.
 * @param self The lock to destroy. May be NULL, in which case this is a no-op that returns success.
 * @returns Success once the object has been released. May return ErrorCode_InvalidOperation if the
 *          native destroy operation fails, in which case the object is not freed.
 */
Error ReadWriteLock_Deconstruct(ReadWriteLock* self);
