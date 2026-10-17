#pragma once
#include <stdbool.h>
#include "wr/WRError.h"
#include "WorldObject.h"
// A sprite object owns its animation playback state (a SpriteAnimationInstance) by value, so the full type is
// needed here. This deliberately couples the world layer to the sprite-animation type: the playback state
// (current frame, fps, running/looping, ...) is world/game state that must be gettable and settable.
#include "SpriteAnimation.h"


/**
 * @file WorldSpriteObject.h
 * @brief A 2D sprite object in a world: a plane that renders a sprite animation, referenced by name.
 *
 * A sprite object is a plane placed in the world with the base object's transform and tint, onto which
 * a sprite animation (resolved from the asset manager by name) is drawn. Like a model object it carries a
 * 1-pixel outline toggle (on by default) and a pixelation-exempt toggle honored by the world renderer.
 *
 * The object owns a SpriteAnimationInstance holding the playback state (current frame, timing, fps, ...). It
 * starts EMPTY (no backing animation, which is valid). Once the sprite is added to a World built with an asset
 * manager, the world loads the animation named here under its own asset user and backs the instance with it;
 * from then on the name changes only through World_SetSpriteAnimationAssetName, which rebinds, so the name and
 * the playing animation always agree. Each (re)bind re-initializes the instance, dropping its event
 * subscriptions, so subscribe after the sprite is added or renamed. Advance the instance
 * (SpriteAnimationInstance_Update) from game logic; the renderer only reads its current frame. The backing
 * animation is BORROWED and must outlive the object.
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

    /** @brief Playback state for this sprite's animation (current frame, timing, fps, running/looping). Starts
     *  empty (no source); set the backing animation with WorldSpriteObject_SetAnimation. Access it (to read the
     *  current frame, or drive fps/frame/step/running/looping) via WorldSpriteObject_GetAnimationInstance. */
    SpriteAnimationInstance _animationInstance;

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
 * @p spriteAnimationAssetName into owned copies, starts with an EMPTY animation instance (no backing
 * animation — set it later with WorldSpriteObject_SetAnimation), and defaults HasOutline to true and
 * OmitPixelation to false. @p spriteAnimationAssetName may be NULL for a fully empty sprite object. Release
 * with the object's Destroy (via WorldObject_Destroy) or by handing it to a World.
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
 * @brief Returns the object's animation-playback instance (borrowed) for reading or driving playback.
 *
 * Use it to read the current frame (SpriteAnimationInstance_GetCurrentFrame) or to get/set the fps, frame
 * index, frame step, running and looping state, and to advance it (SpriteAnimationInstance_Update). The
 * instance starts with no source until WorldSpriteObject_SetAnimation is called.
 * @param self The sprite object; must not be NULL.
 * @returns A borrowed pointer to the embedded instance, valid until the object is destroyed.
 */
static inline SpriteAnimationInstance* WorldSpriteObject_GetAnimationInstance(WorldSpriteObject* self)
{
    return &self->_animationInstance;
}

/**
 * @brief Sets (or clears) the backing animation of a sprite that does not belong to a world.
 *
 * Re-initializes the embedded instance to play @p animation from its default parameters (fps, frame step,
 * running and looping), or to an empty (no-source) instance when @p animation is NULL. Any previous playback
 * state and event subscriptions on the instance are torn down and replaced. The animation is BORROWED and must
 * outlive the object (or this call with NULL). Does not change the stored asset name. If re-initializing
 * fails, the instance is left empty (still constructed).
 *
 * A world binds the animations of its own sprites (see World_AddObject), so this refuses a sprite that
 * belongs to a world, and adding the sprite to a world with an asset manager replaces what was set here.
 * @param self The sprite object; must not be NULL.
 * @param animation The animation to back the instance with (borrowed), or NULL to clear to an empty instance.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self is NULL;
 *          ErrorCode_InvalidOperation if the sprite belongs to a world (its id is not 0).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldSpriteObject_SetAnimation(WorldSpriteObject* self, SpriteAnimation* animation);

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
 * @brief Replaces the referenced sprite animation asset name of a sprite that does not belong to a world.
 *
 * For a sprite in a world use World_SetSpriteAnimationAssetName, which also rebinds the playing animation.
 * @param self The sprite object; must not be NULL.
 * @param spriteAnimationAssetName Borrowed NUL-terminated UTF-8 asset name to clone, or NULL to clear it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_InvalidOperation if the sprite
 *          belongs to a world (its id is not 0).
 */
Error WorldSpriteObject_SetSpriteAnimationAssetName(WorldSpriteObject* self,
    const unsigned char* spriteAnimationAssetName);
