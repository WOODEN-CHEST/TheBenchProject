#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRMath.h"


/**
 * Private shared helpers for the world data modules (WorldObject and its concrete types, World,
 * WorldDTO). Not part of the public API; kept in the source directory so several world implementation
 * files can share small validation/cloning helpers without duplicating them or exposing them.
 */


// Forward declarations (full types in WorldSpriteObject.h and SpriteAnimation.h).
typedef struct WorldSpriteObjectStruct WorldSpriteObject;
typedef struct SpriteAnimationStruct SpriteAnimation;


/**
 * Backs a sprite object's playback instance with @p animation (borrowed), or empties it when NULL, also while
 * the sprite belongs to a world. Only the World calls this (its sprites' animations are held under the world's
 * asset user); the public WorldSpriteObject_SetAnimation refuses sprites that belong to a world. On failure the
 * instance is left empty but constructed. Implemented in WorldSpriteObject.c.
 */
Error WorldSpriteObject_BindAnimation(WorldSpriteObject* self, SpriteAnimation* animation);

/**
 * Replaces a sprite object's animation asset name with an owned clone of @p name (NULL clears it), also while
 * the sprite belongs to a world. Only World_SetSpriteAnimationAssetName calls this, so the name and the bound
 * animation change together. Implemented in WorldSpriteObject.c.
 */
void WorldSpriteObject_ReplaceAnimationAssetName(WorldSpriteObject* self, const unsigned char* name);


/** Returns true if @p value is a real, finite float (not NaN and not +/- infinity). */
static inline bool WorldShared_IsFloatFinite(float value)
{
    return !Math_IsNaNFloat(value) && !Math_IsInfinityFloat(value);
}

/** Returns true if every component of @p value is finite (see WorldShared_IsFloatFinite). */
static inline bool WorldShared_IsVector3Finite(Vector3 value)
{
    return WorldShared_IsFloatFinite(value.x)
        && WorldShared_IsFloatFinite(value.y)
        && WorldShared_IsFloatFinite(value.z);
}

/**
 * Clones a NUL-terminated UTF-8 string into a freshly allocated, owned buffer.
 * @p source may be NULL, in which case *outClone is set to NULL. The allocation aborts on failure
 * (Memory_Allocate never returns NULL), so *outClone is always valid on return. Free the result with
 * Memory_Free.
 */
static inline void WorldShared_CloneString(const unsigned char* source, unsigned char** outClone)
{
    if (source == NULL)
    {
        *outClone = NULL;
        return;
    }

    size_t Length = 0;
    while (source[Length] != (unsigned char)u8'\0')
    {
        Length++;
    }

    unsigned char* Copy = Memory_Allocate(Length + 1);
    Memory_Copy(source, Copy, Length + 1);
    *outClone = Copy;
}
