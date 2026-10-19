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
 * @brief The sprite component: a sprite animation, referenced by its asset name, drawn as a flat image at its object's
 *        position, lit like the models around it (or unlit), turned toward the camera or held by the object's rotation.
 *
 * SHAPE. The image is a quad of the current frame's size in pixels divided by pixels_per_unit (default 32), times the
 * object's X and Y scale. Its pivot (a point of the image, (0, 0) its bottom left, (1, 1) its top right; default the
 * centre) sits at the object's position; flip_x and flip_y mirror it about the pivot. Its orientation is one of:
 * - billboard: the quad faces the camera (its normal points at the camera, its top toward the camera's up);
 * - axis_billboard: the quad turns only around the object's local Y axis to face the camera (trees, lamp posts);
 * - fixed: the quad lies in the object's local XY plane, facing local +Z, turned by the object's rotation (signs,
 *   posters, decals). The image is two-sided.
 *
 * LOOK. Lit sprites (is_lit, the default) take the sun, the moon, the lamps, shadows and fog like models; a billboard's
 * light wraps around it by light_wrap, as around a round thing. Unlit sprites show their colours as emitted light
 * (times emission: neon, fire), still fogged. The alpha mode decides transparency: cutout (alpha test at alpha_cutoff:
 * opaque, depth-correct, unsorted, casts shadows when casts_shadows, outlined), blend (soft edges, smoke: sorted back to
 * front after the opaque world, writes no depth) or additive (glows: adds light, sorted with the blended ones). A decal
 * (is_decal, together with the fixed orientation) lies on a surface: it is drawn after the opaque world, pushed
 * decal_offset metres off its surface, and casts no shadow and has no outline. omit_pixelation draws a cutout sprite
 * that is not a decal at full resolution over the pixelated image.
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
 * sprite_animation, orientation, pivot, pixels_per_unit, flip_x, flip_y, alpha_mode, alpha_cutoff, is_lit, emission,
 * light_wrap, casts_shadows, is_decal, decal_offset, has_outline and omit_pixelation. Every change advances the
 * component's revision.
 */


// Macros.
/** @brief The most bytes a sprite animation's asset name may have (without the terminator). */
#define WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH ((size_t)128)
/** @brief The default pixels per world unit (metre): a 64x32 image is 2 by 1 metres. */
#define WORLD_SPRITE_COMPONENT_DEFAULT_PIXELS_PER_UNIT 32.0f


// Types.
/**
 * @brief How a sprite's quad is turned. Saved by its value: never renumber.
 */
typedef enum WorldSpriteOrientationEnum
{
    /** @brief Faces the camera (the default): the quad's normal points at the camera, its top toward the camera's up. */
    WorldSpriteOrientation_Billboard = 0,
    /** @brief Turns only around the object's local Y axis to face the camera: upright things such as trees and lamp
     *  posts. */
    WorldSpriteOrientation_AxisBillboard = 1,
    /** @brief Uses the object's full rotation: the quad lies in the object's local XY plane, facing local +Z (signs,
     *  posters, decals). */
    WorldSpriteOrientation_Fixed = 2
} WorldSpriteOrientation;

/**
 * @brief How a sprite's transparent texels are drawn. Saved by its value: never renumber.
 */
typedef enum WorldSpriteAlphaModeEnum
{
    /** @brief Alpha test (the default): texels below the alpha cutoff are not drawn, the rest are opaque. Writes depth,
     *  needs no sorting, casts shadows, gets outlines: hard pixel-art edges. */
    WorldSpriteAlphaMode_Cutout = 0,
    /** @brief Alpha blending: soft edges (smoke, stains). Drawn after the opaque world, sorted back to front, writes no
     *  depth, casts no shadow, gets no outline. */
    WorldSpriteAlphaMode_Blend = 1,
    /** @brief Additive: the sprite adds its light (glows, sparks). Sorted with the blended sprites, writes no depth, casts
     *  no shadow, gets no outline. */
    WorldSpriteAlphaMode_Additive = 2
} WorldSpriteAlphaMode;

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
    /** @brief How the quad is turned (default WorldSpriteOrientation_Billboard). */
    WorldSpriteOrientation _orientation;
    /** @brief The point of the image at the object's position, in image units: (0, 0) bottom left, (1, 1) top right
     *  (default (0.5, 0.5), the centre; (0.5, 0) is the bottom centre, for things standing on the ground). Finite; may
     *  lie outside the image. */
    Vector2 _pivot;
    /** @brief Image pixels per world unit (> 0, default WORLD_SPRITE_COMPONENT_DEFAULT_PIXELS_PER_UNIT): the world size is
     *  the frame's size in pixels divided by this, times the object's X and Y scale. */
    float _pixelsPerUnit;
    /** @brief Whether the image is mirrored left to right, about the pivot (default false). */
    bool _isFlippedX;
    /** @brief Whether the image is mirrored top to bottom, about the pivot (default false). */
    bool _isFlippedY;
    /** @brief How transparent texels are drawn (default WorldSpriteAlphaMode_Cutout). */
    WorldSpriteAlphaMode _alphaMode;
    /** @brief The cutout threshold, in [0, 1] (default 0.5): cutout sprites do not draw texels whose alpha is below it. */
    float _alphaCutoff;
    /** @brief Whether the sprite is lit like the models around it (default true); false shows its colours as emitted
     *  light (neon, fire), still fogged. */
    bool _isLit;
    /** @brief Brightness multiplier of an unlit sprite's light (>= 0, default 1); unused while lit. */
    float _emission;
    /** @brief How far light wraps around a lit billboard or axis billboard, in [0, 1] (default 1): 0 lights it as a flat
     *  card facing the camera (dark when lit from the side), 1 as a round thing (half lit from the side, dark only when lit
     *  from straight behind). Fixed sprites are flat cards. */
    float _lightWrap;
    /** @brief Whether a cutout sprite casts shadows (default true); decals, blended and additive sprites never do. */
    bool _castsShadows;
    /** @brief Whether the sprite is a decal lying on a surface (default false; only together with the fixed orientation,
     *  otherwise it is drawn as an ordinary sprite). */
    bool _isDecal;
    /** @brief Metres a decal is pushed off its surface along its normal, toward the side it is seen from (>= 0, default
     *  0.01): raylib has no polygon offset, and this keeps it from flickering against the surface. */
    float _decalOffset;
    /** @brief Whether the renderer draws a 1-pixel outline around the sprite (default true; cutout sprites that are not
     *  decals only). */
    bool _hasOutline;
    /** @brief Whether the sprite is drawn at full resolution over the pixelated image (default false; cutout sprites that
     *  are not decals only). */
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
 * @brief Exempts the sprite from the pixelation pass (drawn crisp at full resolution) or not. Only cutout sprites that
 *        are not decals are drawn crisp; the others stay pixelated.
 * @param self The sprite component; must not be NULL.
 * @param omitPixelation Whether to exempt it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetOmitsPixelation(WorldSpriteComponent* self, bool omitPixelation);

/**
 * @brief Returns how the sprite's quad is turned.
 * @param self The sprite component; must not be NULL.
 * @returns The orientation.
 */
static inline WorldSpriteOrientation WorldSpriteComponent_GetOrientation(const WorldSpriteComponent* self)
{
    return self->_orientation;
}

/**
 * @brief Sets how the sprite's quad is turned.
 * @param self The sprite component; must not be NULL.
 * @param orientation A WorldSpriteOrientation value.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for any other value.
 */
Error WorldSpriteComponent_SetOrientation(WorldSpriteComponent* self, WorldSpriteOrientation orientation);

/**
 * @brief Returns the point of the image that sits at the object's position ((0, 0) bottom left, (1, 1) top right).
 * @param self The sprite component; must not be NULL.
 * @returns The pivot.
 */
static inline Vector2 WorldSpriteComponent_GetPivot(const WorldSpriteComponent* self)
{
    return self->_pivot;
}

/**
 * @brief Sets the point of the image that sits at the object's position: (0.5, 0.5) the centre, (0.5, 0) the bottom
 *        centre (things standing on the ground). It may lie outside the image.
 * @param self The sprite component; must not be NULL.
 * @param pivot The pivot in image units.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange unless both components
 *          are finite (nothing changes then).
 */
Error WorldSpriteComponent_SetPivot(WorldSpriteComponent* self, Vector2 pivot);

/**
 * @brief Returns how many image pixels make one world unit.
 * @param self The sprite component; must not be NULL.
 * @returns The pixels per unit (> 0).
 */
static inline float WorldSpriteComponent_GetPixelsPerUnit(const WorldSpriteComponent* self)
{
    return self->_pixelsPerUnit;
}

/**
 * @brief Sets how many image pixels make one world unit; the world size is the frame's size in pixels divided by it,
 *        times the object's X and Y scale.
 * @param self The sprite component; must not be NULL.
 * @param pixelsPerUnit Pixels per unit; must be finite and above 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetPixelsPerUnit(WorldSpriteComponent* self, float pixelsPerUnit);

/**
 * @brief Returns whether the image is mirrored left to right.
 * @param self The sprite component; must not be NULL.
 * @returns true if mirrored.
 */
static inline bool WorldSpriteComponent_IsFlippedX(const WorldSpriteComponent* self)
{
    return self->_isFlippedX;
}

/**
 * @brief Mirrors the image left to right about the pivot, or not.
 * @param self The sprite component; must not be NULL.
 * @param isFlipped Whether to mirror it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetFlippedX(WorldSpriteComponent* self, bool isFlipped);

/**
 * @brief Returns whether the image is mirrored top to bottom.
 * @param self The sprite component; must not be NULL.
 * @returns true if mirrored.
 */
static inline bool WorldSpriteComponent_IsFlippedY(const WorldSpriteComponent* self)
{
    return self->_isFlippedY;
}

/**
 * @brief Mirrors the image top to bottom about the pivot, or not.
 * @param self The sprite component; must not be NULL.
 * @param isFlipped Whether to mirror it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetFlippedY(WorldSpriteComponent* self, bool isFlipped);

/**
 * @brief Returns how the sprite's transparent texels are drawn.
 * @param self The sprite component; must not be NULL.
 * @returns The alpha mode.
 */
static inline WorldSpriteAlphaMode WorldSpriteComponent_GetAlphaMode(const WorldSpriteComponent* self)
{
    return self->_alphaMode;
}

/**
 * @brief Sets how the sprite's transparent texels are drawn (cutout, blend or additive).
 * @param self The sprite component; must not be NULL.
 * @param alphaMode A WorldSpriteAlphaMode value.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for any other value.
 */
Error WorldSpriteComponent_SetAlphaMode(WorldSpriteComponent* self, WorldSpriteAlphaMode alphaMode);

/**
 * @brief Returns the cutout threshold.
 * @param self The sprite component; must not be NULL.
 * @returns The alpha cutoff, in [0, 1].
 */
static inline float WorldSpriteComponent_GetAlphaCutoff(const WorldSpriteComponent* self)
{
    return self->_alphaCutoff;
}

/**
 * @brief Sets the cutout threshold: a cutout sprite does not draw texels whose alpha is below it.
 * @param self The sprite component; must not be NULL.
 * @param alphaCutoff The threshold; must be in [0, 1].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetAlphaCutoff(WorldSpriteComponent* self, float alphaCutoff);

/**
 * @brief Returns whether the sprite is lit like the models around it.
 * @param self The sprite component; must not be NULL.
 * @returns true if lit; false if its colours are shown as emitted light.
 */
static inline bool WorldSpriteComponent_IsLit(const WorldSpriteComponent* self)
{
    return self->_isLit;
}

/**
 * @brief Lights the sprite like the models around it, or shows its colours as emitted light (neon, fire; still fogged).
 * @param self The sprite component; must not be NULL.
 * @param isLit Whether it is lit.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetLit(WorldSpriteComponent* self, bool isLit);

/**
 * @brief Returns the brightness multiplier of an unlit sprite's light.
 * @param self The sprite component; must not be NULL.
 * @returns The emission (>= 0).
 */
static inline float WorldSpriteComponent_GetEmission(const WorldSpriteComponent* self)
{
    return self->_emission;
}

/**
 * @brief Sets the brightness multiplier of an unlit sprite's light (unused while lit).
 * @param self The sprite component; must not be NULL.
 * @param emission The multiplier; must be finite and >= 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetEmission(WorldSpriteComponent* self, float emission);

/**
 * @brief Returns how far light wraps around a lit billboard or axis billboard.
 * @param self The sprite component; must not be NULL.
 * @returns The wrap, in [0, 1].
 */
static inline float WorldSpriteComponent_GetLightWrap(const WorldSpriteComponent* self)
{
    return self->_lightWrap;
}

/**
 * @brief Sets how far light wraps around a lit billboard or axis billboard: 0 a flat card facing the camera, 1 a round
 *        thing (half lit from the side). Fixed sprites ignore it.
 * @param self The sprite component; must not be NULL.
 * @param lightWrap The wrap; must be in [0, 1].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetLightWrap(WorldSpriteComponent* self, float lightWrap);

/**
 * @brief Returns whether the sprite casts shadows (when it is a cutout sprite that is not a decal).
 * @param self The sprite component; must not be NULL.
 * @returns true if it casts shadows.
 */
static inline bool WorldSpriteComponent_CastsShadows(const WorldSpriteComponent* self)
{
    return self->_castsShadows;
}

/**
 * @brief Sets whether the sprite casts shadows. Only cutout sprites that are not decals cast; it still receives shadows
 *        either way.
 * @param self The sprite component; must not be NULL.
 * @param castsShadows Whether it casts shadows.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetCastsShadows(WorldSpriteComponent* self, bool castsShadows);

/**
 * @brief Returns whether the sprite is marked as a decal (it is drawn as one only with the fixed orientation).
 * @param self The sprite component; must not be NULL.
 * @returns true if marked as a decal.
 */
static inline bool WorldSpriteComponent_IsDecal(const WorldSpriteComponent* self)
{
    return self->_isDecal;
}

/**
 * @brief Marks the sprite as a decal lying on a surface, or not. With the fixed orientation a decal is drawn after the
 *        opaque world, pushed off its surface by the decal offset, with no shadow and no outline; with another
 *        orientation the mark has no effect.
 * @param self The sprite component; must not be NULL.
 * @param isDecal Whether it is a decal.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetDecal(WorldSpriteComponent* self, bool isDecal);

/**
 * @brief Returns how many metres a decal is pushed off its surface.
 * @param self The sprite component; must not be NULL.
 * @returns The offset (>= 0).
 */
static inline float WorldSpriteComponent_GetDecalOffset(const WorldSpriteComponent* self)
{
    return self->_decalOffset;
}

/**
 * @brief Sets how many metres a decal is pushed off its surface along its normal, toward the side it is seen from.
 * @param self The sprite component; must not be NULL.
 * @param decalOffset The offset in metres; must be finite and >= 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetDecalOffset(WorldSpriteComponent* self, float decalOffset);

/**
 * @brief Whether the renderer draws the sprite as a decal: marked as one and with the fixed orientation.
 * @param self The sprite component; must not be NULL.
 * @returns true if it is drawn as a decal.
 */
static inline bool WorldSpriteComponent_IsDrawnAsDecal(const WorldSpriteComponent* self)
{
    return self->_isDecal && (self->_orientation == WorldSpriteOrientation_Fixed);
}
