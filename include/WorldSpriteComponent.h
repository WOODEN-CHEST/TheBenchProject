#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"
// A sprite component owns its animation playback state (a SpriteAnimationInstance) by value, so the full type is
// needed here. This deliberately couples the world layer to the sprite-animation type: the playback state (current
// frame, fps, running/looping, ...) is world/game state that must be gettable and settable.
#include "SpriteAnimation.h"


/**
 * @file WorldSpriteComponent.h
 * @brief The sprite component: a sprite animation, referenced by its asset name, drawn as a camera-facing billboard at
 *        its object's position, sized by its object's X and Y scale and tinted by its tint.
 *
 * The component owns a SpriteAnimationInstance with the playback state (current frame, timing, fps, running,
 * looping). Game logic advances it (SpriteAnimationInstance_Update, the "animation" step of the tick); the renderer
 * only reads its current frame.
 *
 * BINDING. The playing animation always matches the name: when the component is attached and whenever its
 * sprite_animation property changes (through WorldSpriteComponent_SetSpriteAnimation or the table: Properties_Set, a
 * JSON or GHDF read, Properties_CopyTo), the world loads the named animation under its own asset user and restarts the
 * instance from the animation's defaults. Setting the name it already has changes nothing. A world without an asset
 * manager, a NULL name or an animation that fails to load leaves the instance empty (it draws nothing); a failure is
 * logged once, when it happens. Each rebind re-initializes the instance, dropping its event subscriptions, so subscribe
 * after setting the name. The animation is borrowed from the world's asset user, which holds it until the world is
 * destroyed.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Sprite, &component); reach its fields with
 * WorldSpriteComponent_FromComponent. Its properties are the rows of its table ("sprite_component"):
 * sprite_animation, has_outline and omit_pixelation. Every change advances the component's revision.
 */


// Macros.
/** @brief The most bytes a sprite animation's asset name may have (without the terminator). */
#define WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH ((size_t)128)


// Types.
/**
 * @brief The sprite component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldSpriteComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Owned asset name of the sprite animation to play (at most WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH bytes);
     *  NULL plays nothing (the default). */
    unsigned char* _spriteAnimation;
    /** @brief Whether the renderer draws a 1-pixel outline around the sprite (default true). */
    bool _hasOutline;
    /** @brief Whether the sprite is drawn at full resolution over the pixelated image (default false). */
    bool _omitPixelation;
    /** @brief The playback state of the bound animation; empty (no source) while nothing is bound. Never moved or
     *  copied by value. Reach it with WorldSpriteComponent_GetAnimationInstance. */
    SpriteAnimationInstance _animationInstance;
    /** @brief Owned copy of the name the instance was last bound for (NULL for none); runtime only. A name change is
     *  noticed by comparing the two. */
    unsigned char* _boundAnimation;
} WorldSpriteComponent;


// Functions.
/**
 * @brief Returns the sprite component's registry entry (WorldComponentType_Sprite, "sprite").
 * @returns The entry (static).
 */
const WorldComponentTypeInfo* WorldSpriteComponent_GetTypeInfo(void);

/**
 * @brief Returns the sprite component's property table ("sprite_component").
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldSpriteComponent_GetPropertyTable(void);

/**
 * @brief Downcasts a component to a sprite component.
 * @param component The component, or NULL.
 * @returns The sprite component, or NULL when @p component is NULL or of another type.
 */
static inline WorldSpriteComponent* WorldSpriteComponent_FromComponent(WorldComponent* component)
{
    return ((component != NULL) && (component->_type == WorldComponentType_Sprite)) ? (WorldSpriteComponent*)component
        : NULL;
}

/**
 * @brief Upcasts a sprite component to its base.
 * @param self The sprite component; must not be NULL.
 * @returns The embedded base.
 */
static inline WorldComponent* WorldSpriteComponent_AsComponent(WorldSpriteComponent* self)
{
    return &self->Base;
}

/**
 * @brief Returns the borrowed asset name of the sprite animation (may be NULL); valid until it changes.
 * @param self The sprite component; must not be NULL.
 * @returns The NUL-terminated UTF-8 asset name, or NULL.
 */
static inline const unsigned char* WorldSpriteComponent_GetSpriteAnimation(const WorldSpriteComponent* self)
{
    return self->_spriteAnimation;
}

/**
 * @brief Sets the sprite animation to play (a clone of @p spriteAnimation; NULL plays nothing) and binds it.
 *
 * See "BINDING" in the file comment: a new name restarts playback from the animation's defaults and drops the
 * instance's event subscriptions; a missing animation is logged once and leaves the sprite empty without failing.
 * @param self The sprite component; must not be NULL.
 * @param spriteAnimation Borrowed asset name, or NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p spriteAnimation
 *          is not a valid asset name of at most WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH bytes (nothing changes then).
 */
Error WorldSpriteComponent_SetSpriteAnimation(WorldSpriteComponent* self, const unsigned char* spriteAnimation);

/**
 * @brief Returns the playback instance of the bound animation (borrowed; valid while the component exists).
 *
 * Read its current frame (SpriteAnimationInstance_GetCurrentFrame), drive its fps, frame, step, running and looping
 * state, and advance it (SpriteAnimationInstance_Update). It has no source while nothing is bound.
 * @param self The sprite component; must not be NULL.
 * @returns The embedded instance.
 */
static inline SpriteAnimationInstance* WorldSpriteComponent_GetAnimationInstance(WorldSpriteComponent* self)
{
    return &self->_animationInstance;
}

/**
 * @brief Returns whether the renderer outlines the sprite.
 * @param self The sprite component; must not be NULL.
 * @returns true if outlined.
 */
static inline bool WorldSpriteComponent_HasOutline(const WorldSpriteComponent* self)
{
    return self->_hasOutline;
}

/**
 * @brief Turns the sprite's outline on or off.
 * @param self The sprite component; must not be NULL.
 * @param hasOutline Whether to draw the outline.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetHasOutline(WorldSpriteComponent* self, bool hasOutline);

/**
 * @brief Returns whether the sprite is drawn at full resolution over the pixelated image.
 * @param self The sprite component; must not be NULL.
 * @returns true if exempt from the pixelation.
 */
static inline bool WorldSpriteComponent_OmitsPixelation(const WorldSpriteComponent* self)
{
    return self->_omitPixelation;
}

/**
 * @brief Exempts the sprite from the pixelation pass (drawn crisp at full resolution) or not.
 * @param self The sprite component; must not be NULL.
 * @param omitPixelation Whether to exempt it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetOmitsPixelation(WorldSpriteComponent* self, bool omitPixelation);
