#pragma once
#include "WRMemory.h"
#include <stdint.h>
#include <stddef.h>
#include "WRError.h"
#include "WRUserData.h"


// Types.
/**
 * @brief Caller-supplied hook invoked at a single point in a pooled object's lifecycle.
 *
 * One callback type serves all three lifecycle slots (construct, reset, deconstruct). The pool
 * calls it with the in-place address of the object's storage and the const UserData the pool was
 * constructed with, letting the caller manage any resources the object owns. The pool's own
 * bookkeeping (occupancy, free slots) is updated by the pool around the call; the callback must
 * only touch the object's own bytes/owned resources, not pool internals.
 * @param object Address of the object's storage within the pool. Never NULL when called by the
 *        pool. Sized exactly elementSize bytes.
 * @param userData The pool's attached context, passed as a const pointer (never NULL; an empty
 *        UserData is supplied when none was configured).
 * @returns A success Error to proceed, or a non-success Error to signal failure. A non-success
 *          result from ConstructObject/ResetObject aborts the triggering operation and is
 *          propagated to the caller; see the individual functions for how each reacts.
 */
typedef Error (*ObjectPoolObjectLifecycleCallback)(void* object, const UserData* userData);

/**
 * @brief Bundle of optional lifecycle callbacks attached to an ObjectPool.
 *
 * Each slot may be NULL, in which case the pool applies a default behavior (see each field). The
 * struct is copied by value into the pool at construction, so the callbacks (and their identities)
 * are fixed for the pool's lifetime.
 */
typedef struct ObjectPoolLifecycleStruct
{
    /**
     * @brief Invoked once, the first time a given object slot is initialized (its first borrow).
     *
     * Runs before the object is handed to the caller, so it can perform one-time setup of resources
     * the object owns. Not called again when that same slot is later reused. If NULL, no
     * construction step is performed. A non-success return aborts the borrow and is propagated.
     */
    ObjectPoolObjectLifecycleCallback ConstructObject;
    /**
     * @brief Invoked when an object is returned to the pool (and by Clear) to restore it to a clean
     *        reusable state.
     *
     * Lets the caller release or reset whatever the object accumulated while borrowed. If NULL, the
     * pool instead zero-fills the object's bytes. A non-success return leaves the object marked
     * occupied again and is propagated (see ObjectPool_DisposeObject).
     */
    ObjectPoolObjectLifecycleCallback ResetObject;
    /**
     * @brief Invoked once per initialized object during pool teardown to release resources it owns.
     *
     * Called by ObjectPool_Deconstruct for every slot that was ever constructed, regardless of
     * whether it is currently borrowed. If NULL, no per-object teardown is performed. Errors are
     * collected best-effort rather than aborting teardown.
     */
    ObjectPoolObjectLifecycleCallback DeconstructObject;
} ObjectPoolLifecycle;

/**
 * @brief A pool of fixed-size objects that are borrowed and returned to avoid repeated allocation.
 *
 * Hands out raw storage slots of a fixed elementSize. Borrow a slot with
 * ObjectPool_GetNewObject and return it with ObjectPool_DisposeObject; returned slots are reused
 * by later borrows instead of being freed. Storage grows in fixed-capacity sections as needed and
 * is never shrunk until the pool is cleared or deconstructed. Object pointers handed out remain
 * stable for the lifetime of the pool (sections are not relocated), so a borrowed pointer stays
 * valid until the object is returned. Initialize with one of the ObjectPool_Construct functions
 * and release with ObjectPool_Deconstruct. Not thread-safe; external synchronization is required
 * for concurrent use.
 */
typedef struct ObjectPoolStruct
{
    /** @brief Backing store of pool sections (each an internal block of slots plus occupancy/free-slot bookkeeping). */
    GenericBuffer _sections;
    /** @brief Size in bytes of each pooled object. Fixed at construction; must be greater than zero. */
    size_t _elementSize;
    size_t _sectionCapacity; // Object count per section.
    /** @brief Section index where the next borrow begins its search for a free slot; a placement hint, not a correctness invariant. */
    size_t _nextSectionSearchIndex;
    /** @brief The lifecycle callback set (and their NULL/default behavior) configured for this pool. */
    ObjectPoolLifecycle _lifecycle;
    /** @brief Fixed 128-byte caller context passed (as a const pointer) to every lifecycle callback. */
    UserData _userData;
} ObjectPool;


// Functions.
/**
 * @brief Initializes an object pool with the given object size and per-section capacity and no
 *        lifecycle callbacks.
 *
 * Convenience form of ObjectPool_Construct2 with an empty ObjectPoolLifecycle and no UserData: the
 * pool will zero-fill objects on reuse and perform no construct/deconstruct steps. The pool starts
 * empty (no sections allocated); storage is allocated lazily on the first borrow.
 * @param self The pool to initialize. Must not be NULL; must later be released with
 *        ObjectPool_Deconstruct.
 * @param elementSize Size in bytes of each pooled object. Must be greater than zero.
 * @param sectionCapacity Number of objects per allocated section (the growth granularity). Must be
 *        greater than zero.
 * @returns A success Error on success. Raises ErrorCode_IllegalArgument if @p self is NULL, and
 *          ErrorCode_ArgumentOutOfRange if @p elementSize or @p sectionCapacity is zero.
 */
Error ObjectPool_Construct1(ObjectPool* self, size_t elementSize, size_t sectionCapacity);

/**
 * @brief Initializes an object pool with the given object size, per-section capacity, lifecycle
 *        callbacks, and caller context.
 *
 * The @p lifecycle struct is copied by value and the @p userData is copied into the pool, then both
 * are fixed for the pool's lifetime. The pool starts empty (no sections allocated); storage is
 * allocated lazily as objects are borrowed. The configured callbacks govern object setup, reset,
 * and teardown as documented on ObjectPoolLifecycle. Must later be released with
 * ObjectPool_Deconstruct.
 * @param self The pool to initialize. Must not be NULL.
 * @param elementSize Size in bytes of each pooled object. Must be greater than zero.
 * @param sectionCapacity Number of objects per allocated section (the growth granularity). Must be
 *        greater than zero.
 * @param lifecycle The lifecycle callback set. Any slot may be NULL for default behavior; the whole
 *        struct may be zero-initialized. Copied by value.
 * @param userData Caller context (128 bytes) handed as a const pointer to every callback, or NULL
 *        to attach an empty UserData. Copied by value.
 * @returns A success Error on success. Raises ErrorCode_IllegalArgument if @p self is NULL, and
 *          ErrorCode_ArgumentOutOfRange if @p elementSize or @p sectionCapacity is zero.
 */
Error ObjectPool_Construct2(ObjectPool* self,
    size_t elementSize,
    size_t sectionCapacity,
    ObjectPoolLifecycle lifecycle,
    const UserData* userData);

/**
 * @brief Releases all storage owned by the pool, deconstructing every object that was ever
 *        initialized, and resets the pool to an empty state.
 *
 * Invokes the DeconstructObject callback (if configured) once for every slot that was ever
 * constructed across all sections, whether currently borrowed or free, then frees all section
 * memory. After the call the ObjectPool structure is zeroed; any pointers previously handed out by
 * ObjectPool_GetNewObject are dangling and must not be used. Teardown is best-effort: a callback
 * error does not stop the process; the first error encountered is returned and every later error's
 * message is released so none leak. Safe to call on a NULL @p self (returns success).
 * @param self The pool to deconstruct. May be NULL.
 * @returns A success Error if no callback reported failure (including the NULL case); otherwise the
 *          first non-success Error raised by a DeconstructObject callback.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error ObjectPool_Deconstruct(ObjectPool* self);

/**
 * @brief Returns the fixed per-object size in bytes the pool was constructed with.
 *
 * @param self The pool to query. Must not be NULL.
 * @returns The element size in bytes.
 */
static inline size_t ObjectPool_GetElementSize(ObjectPool* self)
{
    return self->_elementSize;
}

/**
 * @brief Returns the per-section capacity (objects per section) the pool was constructed with.
 *
 * This is the granularity by which the pool grows, not a cap on total objects.
 * @param self The pool to query. Must not be NULL.
 * @returns The number of objects each section holds.
 */
static inline size_t ObjectPool_GetSectionCapacity(ObjectPool* self)
{
    return self->_sectionCapacity;
}

/**
 * @brief Borrows an object from the pool, returning a pointer to a free slot of elementSize bytes.
 *
 * Reuses a previously returned slot when one is available, otherwise initializes a fresh slot
 * (growing the pool by a new section if every existing section is full). On a slot's very first use
 * the ConstructObject callback (if configured) runs before the pointer is handed out; reused slots
 * skip construction and instead carry whatever state ResetObject (or the default zero-fill) left
 * them in when they were last returned. The returned object is marked occupied and its pointer is
 * stable until it is returned with ObjectPool_DisposeObject. On any failure @p *outObject is set to
 * NULL.
 * @param self The pool to borrow from. Must not be NULL.
 * @param outObject [out] Receives the borrowed object pointer on success, or NULL on failure. Must
 *        not be NULL.
 * @returns A success Error with @p *outObject pointing at the borrowed slot. Raises
 *          ErrorCode_IllegalArgument if @p self or @p outObject is NULL, and ErrorCode_BufferTooLarge
 *          if the pool would have to grow beyond addressable capacity. May also propagate a
 *          non-success Error from the ConstructObject callback when a new slot is initialized.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error ObjectPool_GetNewObject(ObjectPool* self, void** outObject);

/**
 * @brief Returns a previously borrowed object to the pool, making its slot available for reuse.
 *
 * The @p object pointer must be one returned by ObjectPool_GetNewObject on this same pool; the pool
 * verifies it lies on an exact object boundary within one of its sections. On return the slot is
 * marked free and the ResetObject callback runs (or, if none is configured, the object's bytes are
 * zeroed) to restore it to a clean reusable state; the object's storage is NOT freed. If the reset
 * step fails, the slot is rolled back to occupied and the error is propagated, so the object remains
 * borrowed. Returning an object that is already free (double return of the same slot) is a no-op
 * success. After a successful return the @p object pointer must not be used until borrowed again.
 * @param self The pool to return to. Must not be NULL.
 * @param object The object to return; must be a pointer previously obtained from this pool via
 *        ObjectPool_GetNewObject. Must not be NULL.
 * @returns A success Error on success (including a redundant return of an already-free slot). Raises
 *          ErrorCode_IllegalArgument if @p self or @p object is NULL, or if @p object does not point
 *          at a valid object boundary belonging to this pool. May also propagate a non-success Error
 *          from the ResetObject callback, in which case the object stays borrowed.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error ObjectPool_DisposeObject(ObjectPool* self, void* object);

/**
 * @brief Returns every currently borrowed object to the pool at once, leaving all storage allocated
 *        for reuse.
 *
 * Resets each occupied object via the ResetObject callback (or zero-fill when none is configured)
 * and marks all slots free across every section, without freeing any section memory, so the pool's
 * capacity is preserved for subsequent borrows. Any object pointers previously handed out are
 * logically returned and must not be used until borrowed again. Stops and returns at the first
 * reset failure, leaving the remainder of the pool unprocessed.
 * @param self The pool to clear. Must not be NULL.
 * @returns A success Error once all objects have been reset. Raises ErrorCode_IllegalArgument if
 *          @p self is NULL. May also propagate a non-success Error from the ResetObject callback, in
 *          which case clearing stops at that object.
 * @note May propagate errors from internal calls; consult the documentation of called functions for
 *       the full set.
 */
Error ObjectPool_Clear(ObjectPool* self);
