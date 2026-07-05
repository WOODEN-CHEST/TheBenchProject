#pragma once
#include <stdbool.h>
#include "wr/WRError.h"
#include "WorldObject.h"


/**
 * @file WorldSpriteObject.h
 * @brief A 2D sprite object in a world: a plane that renders a sprite animation, referenced by name.
 *
 * A sprite object is a plane placed in the world with the base object's transform and tint, onto which
 * a sprite animation (resolved from the asset manager by name at render time; see the asset manager) is
 * drawn. Like a model object it carries a 1-pixel outline toggle (on by default) and a pixelation-exempt
 * toggle honored by the world renderer.
 *
 * Create with WorldSpriteObject_Create (which heap-allocates it); ownership normally passes to a World.
 * Because the WorldObject base is the first member, a WorldSpriteObject* is also a valid WorldObject*.
 */


// Types.
/**
 * @brief A 2D sprite object: the base world object plus a referenced sprite animation and render toggles.
 *
 * Underscore-prefixed fields are read-only to outside code; use the accessors. The plain-named boolean
 * toggles are simple flags with no validation and may be set directly.
 */
typedef struct WorldSpriteObjectStruct
{
    /** @brief Abstract base (must be first). Carries id, name, transform and tint. */
    WorldObject Base;

    /** @brief Owned, NUL-terminated UTF-8 name of the sprite animation asset this object renders; may be NULL. */
    unsigned char* _spriteAnimationAssetName;

    /** @brief When true (default), the renderer draws a 1-pixel outline around this object. */
    bool HasOutline;
    /** @brief When true, this object is excluded from the final pixelation pass (drawn at full resolution). */
    bool OmitPixelation;
} WorldSpriteObject;


// Functions.
/**
 * @brief Creates a heap-allocated 2D sprite object referencing a named sprite animation asset.
 *
 * Initializes the base (identity transform, opaque white tint, id 0), clones @p name and
 * @p spriteAnimationAssetName into owned copies, and defaults HasOutline to true and OmitPixelation to
 * false. Release with the object's Destroy (via WorldObject_Destroy) or by handing it to a World.
 * @param name Borrowed NUL-terminated UTF-8 object name to clone, or NULL.
 * @param spriteAnimationAssetName Borrowed NUL-terminated UTF-8 sprite animation asset name to clone, or NULL.
 * @param outObject [out] Receives the new object on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p outObject is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldSpriteObject_Create(const unsigned char* name, const unsigned char* spriteAnimationAssetName,
    WorldSpriteObject** outObject);

/**
 * @brief Upcasts a sprite object to its WorldObject base.
 * @param self The sprite object; must not be NULL.
 * @returns The embedded base pointer.
 */
static inline WorldObject* WorldSpriteObject_AsObject(WorldSpriteObject* self)
{
    return &self->Base;
}

/**
 * @brief Returns the borrowed sprite animation asset name (may be NULL); valid until changed or freed.
 * @param self The sprite object; must not be NULL.
 * @returns The NUL-terminated UTF-8 sprite animation asset name, or NULL if unset.
 */
static inline const unsigned char* WorldSpriteObject_GetSpriteAnimationAssetName(const WorldSpriteObject* self)
{
    return self->_spriteAnimationAssetName;
}

/**
 * @brief Replaces the referenced sprite animation asset name with an owned clone.
 * @param self The sprite object; must not be NULL.
 * @param spriteAnimationAssetName Borrowed NUL-terminated UTF-8 asset name to clone, or NULL to clear it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteObject_SetSpriteAnimationAssetName(WorldSpriteObject* self,
    const unsigned char* spriteAnimationAssetName);
