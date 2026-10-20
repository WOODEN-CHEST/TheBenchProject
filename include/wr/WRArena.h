#pragma once
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WRError.h"
#include "WRMemory.h"


/**
 * An Arena hands out memory of any size and alignment by moving a position forward through large blocks, and takes
 * it back all at once instead of one allocation at a time. An allocation is a few instructions with no per-allocation
 * header, which suits memory that dies together:
 *   - Per-frame scratch: clear a frame arena at the start of every frame (Arena_Clear).
 *   - Per-level data: clear a level arena when the level unloads.
 *   - Temporary work inside a function: take a marker (Arena_GetMarker), allocate, then rewind to the marker
 *     (Arena_RewindToMarker) before returning. Markers nest like a stack.
 * Objects that are created and destroyed one at a time belong in an ObjectPool or on the heap instead, because an
 * arena only gets their memory back at its next clear or rewind.
 *
 * Blocks never move, so memory from an arena stays valid until the clear or rewind that frees it, or until the arena
 * is deconstructed. A growable arena adds blocks as it fills up, and Arena_Clear merges them into one block of their
 * combined size, so an arena that is cleared regularly settles into a single block and stops allocating. Only the
 * blocks are heap allocations (they show up in the Memory_* counters); allocations from the arena are not.
 * Arena_CreateGenericBuffer makes a GenericBuffer whose storage comes from an arena, so the framework's string and
 * buffer writers can build temporary results without heap allocations.
 *
 * Arenas are not thread-safe; give each thread its own.
 */


// Macros.
/**
 * @brief The alignment that suits any standard type (16 bytes on 64-bit platforms), as malloc guarantees.
 *
 * Pass it as the alignment when the type of the memory is unknown or does not matter. Prefer alignof(T) for a known
 * type T, so small allocations are not padded out to this alignment.
 */
#define ARENA_DEFAULT_ALIGNMENT (alignof(max_align_t))

/**
 * @brief The smallest block (64 KiB) a growable arena adds when it fills up.
 */
#define ARENA_MINIMUM_BLOCK_BYTES ((size_t)65536)

/**
 * @brief The largest block (64 MiB) a growable arena adds when it fills up, unless one allocation needs a bigger one.
 *
 * Growth doubles the arena's capacity, so without a limit a large arena would add a block as big as itself for one
 * more small allocation. The block from ArenaConstructOptions::InitialCapacity and the block Arena_Clear merges the
 * blocks into are not limited.
 */
#define ARENA_MAXIMUM_BLOCK_BYTES ((size_t)67108864)


// Types.
/**
 * @brief One block of an arena's memory. Opaque; only the arena module reads or writes it.
 */
typedef struct ArenaBlockStruct ArenaBlock;

/**
 * @brief Parameters that configure an Arena when passed to Arena_Construct2.
 *
 * Start from ArenaConstructOptions_CreateDefault and override fields, or populate it directly.
 */
typedef struct ArenaConstructOptionsStruct
{
    /** @brief Bytes of memory the arena allocates at construction, as its first block. 0 allocates nothing until the
     *  first allocation. Must be greater than zero when IsGrowthEnabled is false. */
    size_t InitialCapacity;
    /** @brief Whether the arena adds blocks when it fills up. When false the arena keeps its single InitialCapacity
     *  block, and an allocation that does not fit returns NULL. */
    bool IsGrowthEnabled;
} ArenaConstructOptions;

/**
 * @brief A bump allocator: hands out memory from large blocks and takes it back all at once.
 *
 * Initialize with Arena_Construct1 or Arena_Construct2, allocate with Arena_Allocate (or its Zeroed and Array forms),
 * free with Arena_Clear or Arena_RewindToMarker, and release with Arena_Deconstruct. Memory handed out stays valid
 * until the clear or rewind that frees it, or until the arena is deconstructed. Move the arena only while nothing
 * holds a pointer to it (buffers from Arena_CreateGenericBuffer do). Not thread-safe.
 */
typedef struct ArenaStruct
{
    /** @brief First byte of the current block's memory, or NULL while the arena has no block. Cached from the block
     *  for the allocation fast path. */
    unsigned char* _blockData;
    /** @brief Bytes of the current block used so far, alignment padding included. */
    size_t _blockOffset;
    /** @brief Size in bytes of the current block's memory; 0 while the arena has no block. */
    size_t _blockCapacity;
    /** @brief First block of the chain, in position order, or NULL while the arena has no block. Owned. */
    ArenaBlock* _firstBlock;
    /** @brief The block allocations come from, or NULL while the arena has no block. The blocks after it are spares a
     *  rewind left behind, reused before new blocks are added. */
    ArenaBlock* _currentBlock;
    /** @brief Combined size in bytes of the blocks before the current one: the position where the current block
     *  starts. */
    size_t _blockStartPosition;
    /** @brief Combined size in bytes of all blocks' memory. */
    size_t _capacity;
    /** @brief The highest position recorded at a clear or rewind; see Arena_GetPeakUsedByteCount. */
    size_t _peakUsedByteCount;
    /** @brief How many times the arena has been cleared. Stamped into markers so a marker from before a clear is
     *  rejected. */
    size_t _clearCount;
    /** @brief Whether the arena adds blocks when it fills up; a fixed-capacity arena keeps its single block. */
    bool _isGrowthEnabled;
} Arena;

/**
 * @brief A saved arena position: pass it to Arena_RewindToMarker to free everything allocated after it was taken.
 *
 * A plain value taken with Arena_GetMarker; copy it freely, there is nothing to release. It stays valid until the
 * arena is cleared or rewound to an earlier marker.
 */
typedef struct ArenaMarkerStruct
{
    /** @brief Bytes of the arena in use when the marker was taken. */
    size_t _position;
    /** @brief The arena's clear count when the marker was taken. */
    size_t _clearCount;
} ArenaMarker;


// Functions.
/**
 * @brief Builds ArenaConstructOptions with the defaults: no initial capacity (nothing is allocated until the first
 *        allocation) and growth enabled.
 * @returns The default options, ready to be customized and passed to Arena_Construct2.
 */
static inline ArenaConstructOptions ArenaConstructOptions_CreateDefault(void)
{
    return (ArenaConstructOptions)
    {
        .InitialCapacity = 0,
        .IsGrowthEnabled = true,
    };
}

/**
 * @brief Initializes an empty, growable arena that allocates nothing until its first allocation.
 *
 * Same as Arena_Construct2 with ArenaConstructOptions_CreateDefault. Release with Arena_Deconstruct.
 * @param self The arena to initialize. Must not be NULL.
 * @returns A success Error. Raises ErrorCode_IllegalArgument if @p self is NULL.
 */
Error Arena_Construct1(Arena* self);

/**
 * @brief Initializes an empty arena from the given options.
 *
 * If options.InitialCapacity is greater than zero, a first block of that many bytes is allocated now; otherwise
 * nothing is allocated until the first allocation. Release with Arena_Deconstruct.
 * @param self The arena to initialize. Must not be NULL.
 * @param options The configuration, passed by value.
 * @returns A success Error. Raises ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_ArgumentOutOfRange if
 *          growth is disabled and options.InitialCapacity is 0, and ErrorCode_BufferTooLarge if
 *          options.InitialCapacity is too large to describe as an allocation. Running out of memory aborts the
 *          process, as Memory_Allocate does.
 */
Error Arena_Construct2(Arena* self, ArenaConstructOptions options);

/**
 * @brief Frees every block the arena owns and leaves it zeroed.
 *
 * All memory handed out by the arena, and every buffer from Arena_CreateGenericBuffer, becomes invalid. Construct the
 * arena again before reusing it.
 * @param self The arena to release. May be NULL (no-op).
 */
void Arena_Deconstruct(Arena* self);

/**
 * @brief Allocates from the arena's next block: the slow path the inline allocators take when the current block is
 *        full.
 *
 * Skips the rest of the current block (that space stays unused until the next clear or rewind) and allocates from the
 * next block: the spare block after it, left behind by a rewind, if the request fits there, otherwise a new block. A
 * new block is the size of the arena's current capacity (doubling it), at least ARENA_MINIMUM_BLOCK_BYTES and at most
 * ARENA_MAXIMUM_BLOCK_BYTES, or the size the request needs if that is larger. Call Arena_Allocate instead; calling
 * this directly wastes the rest of the current block.
 * @param self The arena to allocate from. May be NULL (returns NULL).
 * @param size Size in bytes; 0 is rounded up to 1.
 * @param alignment Alignment in bytes of the returned address; must be a power of two.
 * @returns The allocation, valid until the arena is cleared, rewound past it, or deconstructed. NULL if @p self is
 *          NULL, @p alignment is not a power of two, the arena's growth is disabled, or @p size is too large to
 *          describe as an allocation. Running out of memory aborts the process, as Memory_Allocate does.
 */
void* Arena_AllocateFromNextBlock(Arena* self, size_t size, size_t alignment);

/**
 * @brief Allocates @p size bytes aligned to @p alignment from the arena.
 *
 * The memory is uninitialized. When the current block has room, which is the common case, this is inline and only
 * moves the arena's position forward; otherwise it calls Arena_AllocateFromNextBlock. There is no per-allocation free:
 * the memory comes back when the arena is cleared, or rewound to a marker taken before the allocation.
 * @param self The arena to allocate from. May be NULL (returns NULL).
 * @param size Size in bytes; 0 is rounded up to 1.
 * @param alignment Alignment in bytes of the returned address; must be a power of two. Pass alignof(T) for a known
 *        type T, or ARENA_DEFAULT_ALIGNMENT.
 * @returns The allocation, valid until the arena is cleared, rewound past it, or deconstructed. NULL if @p self is
 *          NULL, @p alignment is not a power of two, @p size is too large to describe as an allocation, or the arena
 *          is fixed-capacity and has no room left. Running out of memory aborts the process, as Memory_Allocate does.
 */
static inline void* Arena_Allocate(Arena* self, size_t size, size_t alignment)
{
    if ((self == NULL) || (alignment == 0) || ((alignment & (alignment - 1)) != 0))
    {
        return NULL;
    }

    size_t ByteCount = (size == 0) ? 1 : size;
    // Integer arithmetic, because the arena may have no block yet and pointer arithmetic on NULL is undefined.
    size_t Padding = (size_t)((0U - ((uintptr_t)self->_blockData + self->_blockOffset)) & (alignment - 1));
    size_t RemainingByteCount = self->_blockCapacity - self->_blockOffset;

    if ((Padding > RemainingByteCount) || (ByteCount > (RemainingByteCount - Padding)))
    {
        return Arena_AllocateFromNextBlock(self, ByteCount, alignment);
    }

    unsigned char* Allocation = self->_blockData + self->_blockOffset + Padding;
    self->_blockOffset += Padding + ByteCount;
    return Allocation;
}

/**
 * @brief Allocates @p size bytes aligned to @p alignment from the arena and sets them to zero.
 *
 * Same as Arena_Allocate, followed by zeroing the memory.
 * @param self The arena to allocate from. May be NULL (returns NULL).
 * @param size Size in bytes; 0 is rounded up to 1.
 * @param alignment Alignment in bytes of the returned address; must be a power of two.
 * @returns The zeroed allocation, or NULL in the same cases as Arena_Allocate.
 */
static inline void* Arena_AllocateZeroed(Arena* self, size_t size, size_t alignment)
{
    void* Allocation = Arena_Allocate(self, size, alignment);

    if (Allocation != NULL)
    {
        Memory_Zero(Allocation, size);
    }

    return Allocation;
}

/**
 * @brief Allocates an array of @p elementCount elements of @p elementSize bytes each, aligned to @p alignment.
 *
 * Same as Arena_Allocate with elementCount * elementSize bytes, except that a product too large for size_t returns
 * NULL instead of wrapping. The memory is uninitialized.
 * @param self The arena to allocate from. May be NULL (returns NULL).
 * @param elementCount Number of elements; 0 allocates 1 byte, as Arena_Allocate does for 0 bytes.
 * @param elementSize Size in bytes of one element.
 * @param alignment Alignment in bytes of the returned address; must be a power of two. Usually alignof of the element
 *        type.
 * @returns The allocation, or NULL in the same cases as Arena_Allocate, and when elementCount * elementSize overflows
 *          size_t.
 */
static inline void* Arena_AllocateArray(Arena* self, size_t elementCount, size_t elementSize, size_t alignment)
{
    size_t ByteCount = 0;

    if (!Memory_TryMultiplySize(elementCount, elementSize, &ByteCount))
    {
        return NULL;
    }

    return Arena_Allocate(self, ByteCount, alignment);
}

/**
 * @brief Returns how many bytes of the arena are in use: its position.
 *
 * Includes alignment padding, and the unused ends of blocks the arena moved past because an allocation did not fit.
 * @param self The arena. Must not be NULL.
 * @returns The used byte count.
 */
static inline size_t Arena_GetUsedByteCount(const Arena* self)
{
    return self->_blockStartPosition + self->_blockOffset;
}

/**
 * @brief Returns the combined size in bytes of the arena's blocks.
 *
 * @param self The arena. Must not be NULL.
 * @returns The capacity in bytes; 0 while the arena has no block.
 */
static inline size_t Arena_GetCapacity(const Arena* self)
{
    return self->_capacity;
}

/**
 * @brief Returns the most bytes the arena has had in use at once since it was constructed.
 *
 * Useful for choosing ArenaConstructOptions::InitialCapacity: for example, log it for a frame arena at the end of a
 * play session.
 * @param self The arena. Must not be NULL.
 * @returns The peak used byte count.
 */
static inline size_t Arena_GetPeakUsedByteCount(const Arena* self)
{
    size_t UsedByteCount = Arena_GetUsedByteCount(self);

    return (UsedByteCount > self->_peakUsedByteCount) ? UsedByteCount : self->_peakUsedByteCount;
}

/**
 * @brief Returns a marker for the arena's current position, to rewind to later with Arena_RewindToMarker.
 *
 * @param self The arena. Must not be NULL.
 * @returns The marker.
 */
static inline ArenaMarker Arena_GetMarker(const Arena* self)
{
    return (ArenaMarker)
    {
        ._position = Arena_GetUsedByteCount(self),
        ._clearCount = self->_clearCount,
    };
}

/**
 * @brief Frees everything allocated from the arena since @p marker was taken, keeping the memory for reuse.
 *
 * Markers nest like a stack: rewinding to a marker also invalidates every marker taken after it. A marker taken
 * before the last clear, or one ahead of the current position (already rewound past), is rejected. Rewinding to a
 * marker taken after the one you last rewound to is a bug the arena cannot always detect, because the position may
 * have been reached again by newer allocations. Blocks after the new position are kept as spares. O(1) when the marker
 * is in the current block, otherwise O(number of blocks).
 * @param self The arena to rewind. May be NULL (returns false).
 * @param marker A marker from Arena_GetMarker on this arena.
 * @returns true on success, including for a marker at the current position (which frees nothing). false, changing
 *          nothing, if @p self is NULL or @p marker is rejected.
 */
bool Arena_RewindToMarker(Arena* self, ArenaMarker marker);

/**
 * @brief Frees everything allocated from the arena, keeping its memory for reuse.
 *
 * If the arena has grown into several blocks, they are freed and replaced by one block of their combined size, so the
 * next round of allocations fits in a single block: an arena that is cleared regularly (such as once a frame) settles
 * into one block and stops allocating. Every marker taken before the clear becomes invalid. To give the memory back to
 * the system, deconstruct the arena and construct it again.
 * @param self The arena to clear. May be NULL (no-op).
 */
void Arena_Clear(Arena* self);

/**
 * @brief Initializes a growable GenericBuffer whose storage comes from the arena instead of the heap.
 *
 * The buffer starts empty and works like any growable GenericBuffer, so every function that writes into a
 * GenericBuffer can write into it. When it grows while its storage is the arena's latest allocation, the storage is
 * extended in place; otherwise new storage is allocated from the arena, the old storage's bytes are copied over, and
 * the old storage stays unused in the arena until the next clear or rewind. Storage is aligned to
 * ARENA_DEFAULT_ALIGNMENT.
 *
 * The storage belongs to the arena: never pass the buffer's data to Memory_Free or give the buffer to anything that
 * frees it, and there is nothing to release when you are done with it. The buffer is unusable once the arena is
 * cleared or deconstructed, or rewound to a marker taken before the buffer last grew. It keeps a pointer to the arena,
 * so the arena must not move while the buffer is in use.
 * @param self The arena that supplies the storage. May be NULL (returns false).
 * @param buffer [out] The buffer to initialize. May be NULL (returns false).
 * @param initialCapacity Capacity in ELEMENTS to allocate now; 0 allocates nothing until the buffer first grows.
 * @param elementSize Size in bytes of one element; must be greater than zero.
 * @returns true on success. false if @p self or @p buffer is NULL or @p elementSize is 0, leaving @p buffer unchanged.
 *          false if the initial capacity could not be allocated from the arena (too large, or a fixed-capacity arena
 *          is full), in which case @p buffer is still initialized, empty and usable.
 */
bool Arena_CreateGenericBuffer(Arena* self, GenericBuffer* buffer, size_t initialCapacity, size_t elementSize);
