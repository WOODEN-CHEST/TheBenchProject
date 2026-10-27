#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "wr/WRError.h"
#include "PropertyDescriptor.h"
#include "WorldComponent.h"
#include "WorldSprite.h"
// A sprite component owns its animation playback state (a SpriteAnimationInstance) by value, so the full type is
// needed here. This deliberately couples the world layer to the sprite-animation type: the playback state (current
// frame, fps, running/looping, ...) is world/game state that must be gettable and settable.
#include "SpriteAnimation.h"


/**
 * @file WorldSpriteComponent.h
 * @brief The sprite component: a world sprite, referenced by its asset name, drawn as a flat image at its object's
 *        position, and the settings this placement overrides.
 *
 * The component names a WORLD SPRITE (WorldSprite.h: the sprite animation to play and how it stands, is lit and is seen
 * in the world), so changing the name swaps the sprite and keeps the object.
 *
 * SHAPE. The image is a quad of the current frame's size in pixels divided by pixels_per_unit, times the object's X and
 * Y scale. Its pivot (a point of the image, (0, 0) its bottom left, (1, 1) its top right) sits at the object's
 * position; flip_x and flip_y mirror it about the pivot. Its orientation is one of:
 * - billboard: the quad faces the camera (its normal points at the camera, its top toward the camera's up);
 * - axis_billboard: the quad turns only around the object's local Y axis to face the camera (trees, lamp posts);
 * - fixed: the quad lies in the object's local XY plane, facing local +Z, turned by the object's rotation (signs,
 *   posters, decals). The image is two-sided.
 * The object's position, rotation and scale here are its world ones: a parent's placement carries the sprite along.
 *
 * LOOK. Lit sprites take the sun, the moon, the lamps, shadows and the haze like models; a billboard's light wraps around
 * it by light_wrap, as around a round thing. Unlit sprites show their colours as emitted light (times emission: neon,
 * fire), still hazed (haze multiplies the haze in front of a sprite: 0 as if the air were clear). The alpha mode decides transparency: cutout (alpha test at alpha_cutoff: opaque, depth-correct,
 * unsorted, casts shadows when casts_shadows, outlined), blend (soft edges, smoke: sorted back to front after the opaque
 * world, writes no depth) or additive (glows: adds light, sorted with the blended ones). A decal (is_decal, together with
 * the fixed orientation) lies on a surface: it is drawn after the opaque world, pushed decal_offset metres off its
 * surface, and casts no shadow and has no outline. omit_pixelation draws a cutout sprite that is not a decal at full
 * resolution over the pixelated image.
 *
 * OVERRIDES. Every setting of the world sprite (WorldSpriteSetting) can be overridden by the placement: a pair of
 * properties, an override flag ("overrides_" + the setting's key, false by default: the world sprite's value applies)
 * and the placement's own value. The value is kept, and saved, while its flag is off, so switching the flag back on
 * restores it; there are no sentinel values and no "inherit" states. The getters of the values return the placement's
 * own values; WorldSpriteComponent_ResolveSettings turns the world sprite's settings and the
 * overrides into the settings that apply. The draw distance also has a plain multiplier (1 changes nothing, 2 = seen
 * twice as far), which applies to whichever distance is in effect.
 *
 * BINDING. The playing sprite always matches the name: when the component is attached and whenever its world_sprite
 * property changes (through WorldSpriteComponent_SetWorldSprite or the table: Properties_Set, a JSON or GHDF read,
 * Properties_CopyTo), the world loads the named world sprite (and with it its animation) under its own asset user and
 * restarts the instance from the animation's defaults. Setting the name it already has changes nothing. A world without
 * an asset manager, a NULL name or a world sprite that fails to load leaves the instance empty (it draws nothing); a
 * failure is logged once, when it happens. Each rebind re-initializes the instance, dropping its event subscriptions, so
 * subscribe after setting the name. The world sprite is borrowed from the world's asset user, which holds it until the
 * world is destroyed.
 *
 * The component owns a SpriteAnimationInstance with the playback state (current frame, timing, fps, running,
 * looping). Game logic advances it (SpriteAnimationInstance_Update, the "animation" step of the tick); the renderer
 * only reads its current frame.
 *
 * Added with World_AddComponent(world, id, WorldComponentType_Sprite, &component); reach its fields with
 * WorldSpriteComponent_FromComponent. Its properties are the rows of its table ("sprite_component"), in this order:
 * world_sprite; then for each setting its flag and its value: orientation, pivot, pixels_per_unit, flip_x, flip_y,
 * alpha_mode, alpha_cutoff, is_lit, emission, light_wrap, casts_shadows, is_decal, decal_offset, has_outline,
 * omit_pixelation, haze and draw_distance (followed by draw_distance_multiplier) and is_draw_distance_fixed. Every change
 * advances the component's revision.
 */


// Macros.
/** @brief The most bytes a world sprite's asset name may have (without the terminator). */
#define WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH ((size_t)128)


// Types.
/**
 * @brief The settings a placement can override, each with its own flag (WorldSpriteComponent_IsOverridden), in the
 *        table's order. Every one is a WorldSpriteSettings field.
 */
typedef enum WorldSpriteSettingEnum
{
    /** @brief How the quad is turned (orientation). */
    WorldSpriteSetting_Orientation,
    /** @brief The point of the image at the object's position (pivot). */
    WorldSpriteSetting_Pivot,
    /** @brief Image pixels per metre (pixels_per_unit). */
    WorldSpriteSetting_PixelsPerUnit,
    /** @brief Mirrored left to right (flip_x). */
    WorldSpriteSetting_FlipX,
    /** @brief Mirrored top to bottom (flip_y). */
    WorldSpriteSetting_FlipY,
    /** @brief The transparency (alpha_mode). */
    WorldSpriteSetting_AlphaMode,
    /** @brief The cutout threshold (alpha_cutoff). */
    WorldSpriteSetting_AlphaCutoff,
    /** @brief Lit or glowing (is_lit). */
    WorldSpriteSetting_IsLit,
    /** @brief An unlit sprite's brightness (emission). */
    WorldSpriteSetting_Emission,
    /** @brief How far light wraps around a billboard (light_wrap). */
    WorldSpriteSetting_LightWrap,
    /** @brief Casting shadows (casts_shadows). */
    WorldSpriteSetting_CastsShadows,
    /** @brief Lying on a surface (is_decal). */
    WorldSpriteSetting_IsDecal,
    /** @brief A decal's lift off its surface (decal_offset). */
    WorldSpriteSetting_DecalOffset,
    /** @brief The outline (has_outline). */
    WorldSpriteSetting_HasOutline,
    /** @brief Drawing at full resolution (omit_pixelation). */
    WorldSpriteSetting_OmitPixelation,
    /** @brief How hazy it looks at a distance (haze). */
    WorldSpriteSetting_Haze,
    /** @brief The draw distance (draw_distance). */
    WorldSpriteSetting_DrawDistance,
    /** @brief Keeping the draw distance fixed against the quality setting (is_draw_distance_fixed). */
    WorldSpriteSetting_IsDrawDistanceFixed,
    /** @brief The number of settings; not a setting. */
    WorldSpriteSetting_Count
} WorldSpriteSetting;

/**
 * @brief The sprite component. Underscore-prefixed fields are read-only: use the accessors and setters (or the table).
 */
typedef struct WorldSpriteComponentStruct
{
    /** @brief The component base (must be first). */
    WorldComponent Base;
    /** @brief Owned asset name of the world sprite to show (at most WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH bytes); NULL
     *  shows nothing (the default). */
    unsigned char* _worldSprite;
    /** @brief Per setting (indexed by WorldSpriteSetting), whether this placement's own value replaces its world
     *  sprite's (default false). */
    bool _overrides[WorldSpriteSetting_Count];
    /** @brief This placement's orientation (default WorldSpriteOrientation_Billboard). */
    WorldSpriteOrientation _orientation;
    /** @brief This placement's pivot, in image units (default (0.5, 0.5), the centre); finite. */
    Vector2 _pivot;
    /** @brief This placement's image pixels per world unit (> 0, default WORLD_SPRITE_DEFAULT_PIXELS_PER_UNIT). */
    float _pixelsPerUnit;
    /** @brief This placement's "mirrored left to right" (default false). */
    bool _isFlippedX;
    /** @brief This placement's "mirrored top to bottom" (default false). */
    bool _isFlippedY;
    /** @brief This placement's transparency (default WorldSpriteAlphaMode_Cutout). */
    WorldSpriteAlphaMode _alphaMode;
    /** @brief This placement's cutout threshold, in [0, 1] (default WORLD_SPRITE_DEFAULT_ALPHA_CUTOFF). */
    float _alphaCutoff;
    /** @brief This placement's "lit like the models around it" (default true). */
    bool _isLit;
    /** @brief This placement's brightness of an unlit sprite's light (>= 0, default 1). */
    float _emission;
    /** @brief This placement's light wrap, in [0, 1] (default 1). */
    float _lightWrap;
    /** @brief This placement's "casts shadows" (default true). */
    bool _castsShadows;
    /** @brief This placement's "a decal lying on a surface" (default false). */
    bool _isDecal;
    /** @brief This placement's decal lift, in metres (>= 0, default WORLD_SPRITE_DEFAULT_DECAL_OFFSET). */
    float _decalOffset;
    /** @brief This placement's outline setting (default true). */
    bool _hasOutline;
    /** @brief This placement's "drawn at full resolution over the pixelated image" (default false). */
    bool _omitPixelation;
    /** @brief This placement's haze multiplier, in [0, WORLD_SPRITE_MAX_HAZE] (default 1: the real air's haze; 0 as if
     *  the air were clear). */
    float _haze;
    /** @brief This placement's draw distance, in metres, above 0 (default WORLD_SPRITE_DEFAULT_DRAW_DISTANCE). */
    float _drawDistance;
    /** @brief Multiplies the draw distance in effect (the world sprite's, its kind's or this placement's), above 0
     *  (default 1; 2 = seen twice as far). A plain setting, not an override. */
    float _drawDistanceMultiplier;
    /** @brief This placement's "the quality setting leaves the draw distance as it is" (default false). */
    bool _isDrawDistanceFixed;
    /** @brief The playback state of the bound world sprite's animation; empty (no source) while nothing is bound. Never
     *  moved or copied by value. Reach it with WorldSpriteComponent_GetAnimationInstance. */
    SpriteAnimationInstance _animationInstance;
    /** @brief Owned copy of the name the instance was last bound for (NULL for none); runtime only. A name change is
     *  noticed by comparing the two. */
    unsigned char* _boundName;
    /** @brief The world sprite the instance plays, borrowed from the world's asset user; NULL while nothing is bound.
     *  Runtime only. */
    const WorldSprite* _boundWorldSprite;
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
 * @brief Returns the borrowed asset name of the world sprite (may be NULL); valid until it changes.
 * @param self The sprite component; must not be NULL.
 * @returns The NUL-terminated UTF-8 asset name, or NULL.
 */
static inline const unsigned char* WorldSpriteComponent_GetWorldSprite(const WorldSpriteComponent* self)
{
    return self->_worldSprite;
}

/**
 * @brief Sets the world sprite to show (a clone of @p worldSprite; NULL shows nothing) and binds it.
 *
 * See "BINDING" in the file comment: a new name restarts playback from the animation's defaults and drops the
 * instance's event subscriptions; a missing world sprite is logged once and leaves the sprite empty without failing. The
 * object keeps its id, tags, data, children and overrides.
 * @param self The sprite component; must not be NULL.
 * @param worldSprite Borrowed asset name, or NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p worldSprite is not
 *          a valid asset name of at most WORLD_SPRITE_COMPONENT_NAME_MAX_LENGTH bytes (nothing changes then).
 */
Error WorldSpriteComponent_SetWorldSprite(WorldSpriteComponent* self, const unsigned char* worldSprite);

/**
 * @brief Returns the world sprite the component shows: the one its name was last bound to.
 * @param self The sprite component; must not be NULL.
 * @returns The world sprite (borrowed from the world's asset user: valid while the world is), or NULL while nothing is
 *          bound (no name, no asset manager, or a world sprite that failed to load).
 */
static inline const WorldSprite* WorldSpriteComponent_GetBoundWorldSprite(const WorldSpriteComponent* self)
{
    return self->_boundWorldSprite;
}

/**
 * @brief Returns the playback instance of the bound world sprite's animation (borrowed; valid while the component
 *        exists).
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
 * @brief Returns whether this placement overrides one of its world sprite's settings (the setting's override flag).
 * @param self The sprite component; must not be NULL.
 * @param setting The setting.
 * @returns true when the placement's own value applies; false when the world sprite's does (and for a value outside the
 *          enum).
 */
bool WorldSpriteComponent_IsOverridden(const WorldSpriteComponent* self, WorldSpriteSetting setting);

/**
 * @brief Sets one setting's override flag: on, the placement's own value applies; off, the world sprite's (the own value
 *        is kept).
 * @param self The sprite component; must not be NULL.
 * @param setting The setting.
 * @param isOverridden Whether the placement's own value applies.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value outside the
 *          enum.
 */
Error WorldSpriteComponent_SetOverridden(WorldSpriteComponent* self, WorldSpriteSetting setting, bool isOverridden);

/**
 * @brief Returns this placement's own orientation (it applies while WorldSpriteSetting_Orientation is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The orientation.
 */
static inline WorldSpriteOrientation WorldSpriteComponent_GetOrientation(const WorldSpriteComponent* self)
{
    return self->_orientation;
}

/**
 * @brief Sets this placement's own orientation (it applies while WorldSpriteSetting_Orientation is overridden).
 * @param self The sprite component; must not be NULL.
 * @param orientation A WorldSpriteOrientation value.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for any other value.
 */
Error WorldSpriteComponent_SetOrientation(WorldSpriteComponent* self, WorldSpriteOrientation orientation);

/**
 * @brief Returns this placement's own pivot ((0, 0) bottom left, (1, 1) top right; it applies while
 *        WorldSpriteSetting_Pivot is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The pivot.
 */
static inline Vector2 WorldSpriteComponent_GetPivot(const WorldSpriteComponent* self)
{
    return self->_pivot;
}

/**
 * @brief Sets this placement's own pivot: (0.5, 0.5) the centre, (0.5, 0) the bottom centre (things standing on the
 *        ground). It may lie outside the image; it applies while WorldSpriteSetting_Pivot is overridden.
 * @param self The sprite component; must not be NULL.
 * @param pivot The pivot in image units.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange unless both components
 *          are finite (nothing changes then).
 */
Error WorldSpriteComponent_SetPivot(WorldSpriteComponent* self, Vector2 pivot);

/**
 * @brief Returns this placement's own pixels per world unit (it applies while WorldSpriteSetting_PixelsPerUnit is
 *        overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The pixels per unit (> 0).
 */
static inline float WorldSpriteComponent_GetPixelsPerUnit(const WorldSpriteComponent* self)
{
    return self->_pixelsPerUnit;
}

/**
 * @brief Sets this placement's own pixels per world unit (it applies while WorldSpriteSetting_PixelsPerUnit is
 *        overridden); the world size is the frame's size in pixels divided by it, times the object's X and Y scale.
 * @param self The sprite component; must not be NULL.
 * @param pixelsPerUnit Pixels per unit; must be finite and above 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetPixelsPerUnit(WorldSpriteComponent* self, float pixelsPerUnit);

/**
 * @brief Returns this placement's own "mirrored left to right" (it applies while WorldSpriteSetting_FlipX is
 *        overridden).
 * @param self The sprite component; must not be NULL.
 * @returns true if mirrored.
 */
static inline bool WorldSpriteComponent_IsFlippedX(const WorldSpriteComponent* self)
{
    return self->_isFlippedX;
}

/**
 * @brief Sets this placement's own "mirrored left to right, about the pivot" (it applies while WorldSpriteSetting_FlipX
 *        is overridden: one tree asset placed mirrored).
 * @param self The sprite component; must not be NULL.
 * @param isFlipped Whether to mirror it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetFlippedX(WorldSpriteComponent* self, bool isFlipped);

/**
 * @brief Returns this placement's own "mirrored top to bottom" (it applies while WorldSpriteSetting_FlipY is
 *        overridden).
 * @param self The sprite component; must not be NULL.
 * @returns true if mirrored.
 */
static inline bool WorldSpriteComponent_IsFlippedY(const WorldSpriteComponent* self)
{
    return self->_isFlippedY;
}

/**
 * @brief Sets this placement's own "mirrored top to bottom, about the pivot" (it applies while WorldSpriteSetting_FlipY
 *        is overridden).
 * @param self The sprite component; must not be NULL.
 * @param isFlipped Whether to mirror it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetFlippedY(WorldSpriteComponent* self, bool isFlipped);

/**
 * @brief Returns this placement's own transparency (it applies while WorldSpriteSetting_AlphaMode is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The alpha mode.
 */
static inline WorldSpriteAlphaMode WorldSpriteComponent_GetAlphaMode(const WorldSpriteComponent* self)
{
    return self->_alphaMode;
}

/**
 * @brief Sets this placement's own transparency: cutout, blend or additive (it applies while
 *        WorldSpriteSetting_AlphaMode is overridden).
 * @param self The sprite component; must not be NULL.
 * @param alphaMode A WorldSpriteAlphaMode value.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for any other value.
 */
Error WorldSpriteComponent_SetAlphaMode(WorldSpriteComponent* self, WorldSpriteAlphaMode alphaMode);

/**
 * @brief Returns this placement's own cutout threshold (it applies while WorldSpriteSetting_AlphaCutoff is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The alpha cutoff, in [0, 1].
 */
static inline float WorldSpriteComponent_GetAlphaCutoff(const WorldSpriteComponent* self)
{
    return self->_alphaCutoff;
}

/**
 * @brief Sets this placement's own cutout threshold: a cutout sprite does not draw texels whose alpha is below it (it
 *        applies while WorldSpriteSetting_AlphaCutoff is overridden).
 * @param self The sprite component; must not be NULL.
 * @param alphaCutoff The threshold; must be in [0, 1].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetAlphaCutoff(WorldSpriteComponent* self, float alphaCutoff);

/**
 * @brief Returns this placement's own "lit like the models around it" (it applies while WorldSpriteSetting_IsLit is
 *        overridden).
 * @param self The sprite component; must not be NULL.
 * @returns true if lit; false if its colours are shown as emitted light.
 */
static inline bool WorldSpriteComponent_IsLit(const WorldSpriteComponent* self)
{
    return self->_isLit;
}

/**
 * @brief Sets this placement's own lighting: lit like the models around it, or its colours shown as emitted light
 *        (neon, fire; still hazed). It applies while WorldSpriteSetting_IsLit is overridden.
 * @param self The sprite component; must not be NULL.
 * @param isLit Whether it is lit.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetLit(WorldSpriteComponent* self, bool isLit);

/**
 * @brief Returns this placement's own brightness of an unlit sprite's light (it applies while
 *        WorldSpriteSetting_Emission is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The emission (>= 0).
 */
static inline float WorldSpriteComponent_GetEmission(const WorldSpriteComponent* self)
{
    return self->_emission;
}

/**
 * @brief Sets this placement's own brightness of an unlit sprite's light (unused while lit; it applies while
 *        WorldSpriteSetting_Emission is overridden).
 * @param self The sprite component; must not be NULL.
 * @param emission The multiplier; must be finite and >= 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetEmission(WorldSpriteComponent* self, float emission);

/**
 * @brief Returns this placement's own light wrap (it applies while WorldSpriteSetting_LightWrap is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The wrap, in [0, 1].
 */
static inline float WorldSpriteComponent_GetLightWrap(const WorldSpriteComponent* self)
{
    return self->_lightWrap;
}

/**
 * @brief Sets this placement's own light wrap: how far light wraps around a lit billboard or axis billboard, 0 a flat
 *        card facing the camera, 1 a round thing (half lit from the side). Fixed sprites ignore it; it applies while
 *        WorldSpriteSetting_LightWrap is overridden.
 * @param self The sprite component; must not be NULL.
 * @param lightWrap The wrap; must be in [0, 1].
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetLightWrap(WorldSpriteComponent* self, float lightWrap);

/**
 * @brief Returns this placement's own "casts shadows" (it applies while WorldSpriteSetting_CastsShadows is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns true if it casts shadows.
 */
static inline bool WorldSpriteComponent_CastsShadows(const WorldSpriteComponent* self)
{
    return self->_castsShadows;
}

/**
 * @brief Sets this placement's own "casts shadows" (only cutout sprites that are not decals cast; it receives shadows
 *        either way). It applies while WorldSpriteSetting_CastsShadows is overridden.
 * @param self The sprite component; must not be NULL.
 * @param castsShadows Whether it casts shadows.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetCastsShadows(WorldSpriteComponent* self, bool castsShadows);

/**
 * @brief Returns this placement's own "a decal" (it applies while WorldSpriteSetting_IsDecal is overridden; a decal is
 *        drawn as one only with the fixed orientation).
 * @param self The sprite component; must not be NULL.
 * @returns true if marked as a decal.
 */
static inline bool WorldSpriteComponent_IsDecal(const WorldSpriteComponent* self)
{
    return self->_isDecal;
}

/**
 * @brief Sets this placement's own "a decal lying on a surface": with the fixed orientation it is drawn after the opaque
 *        world, pushed off its surface by the decal offset, with no shadow and no outline; with another orientation the
 *        mark has no effect. It applies while WorldSpriteSetting_IsDecal is overridden.
 * @param self The sprite component; must not be NULL.
 * @param isDecal Whether it is a decal.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetDecal(WorldSpriteComponent* self, bool isDecal);

/**
 * @brief Returns this placement's own decal lift, in metres (it applies while WorldSpriteSetting_DecalOffset is
 *        overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The offset (>= 0).
 */
static inline float WorldSpriteComponent_GetDecalOffset(const WorldSpriteComponent* self)
{
    return self->_decalOffset;
}

/**
 * @brief Sets this placement's own decal lift: metres a decal is pushed off its surface along its normal, toward the side
 *        it is seen from. It applies while WorldSpriteSetting_DecalOffset is overridden.
 * @param self The sprite component; must not be NULL.
 * @param decalOffset The offset in metres; must be finite and >= 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange otherwise (nothing changes
 *          then).
 */
Error WorldSpriteComponent_SetDecalOffset(WorldSpriteComponent* self, float decalOffset);

/**
 * @brief Returns this placement's own outline setting (it applies while WorldSpriteSetting_HasOutline is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns true if outlined.
 */
static inline bool WorldSpriteComponent_HasOutline(const WorldSpriteComponent* self)
{
    return self->_hasOutline;
}

/**
 * @brief Sets this placement's own outline setting (cutout sprites that are not decals; it applies while
 *        WorldSpriteSetting_HasOutline is overridden).
 * @param self The sprite component; must not be NULL.
 * @param hasOutline Whether to draw the outline.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetHasOutline(WorldSpriteComponent* self, bool hasOutline);

/**
 * @brief Returns this placement's own "drawn at full resolution over the pixelated image" (it applies while
 *        WorldSpriteSetting_OmitPixelation is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns true if exempt from the pixelation.
 */
static inline bool WorldSpriteComponent_OmitsPixelation(const WorldSpriteComponent* self)
{
    return self->_omitPixelation;
}

/**
 * @brief Sets this placement's own "drawn at full resolution over the pixelated image" (only cutout sprites that are not
 *        decals are drawn crisp; the others stay pixelated). It applies while WorldSpriteSetting_OmitPixelation is
 *        overridden.
 * @param self The sprite component; must not be NULL.
 * @param omitPixelation Whether to exempt it.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetOmitsPixelation(WorldSpriteComponent* self, bool omitPixelation);

/**
 * @brief Returns this placement's own haze multiplier (it applies while WorldSpriteSetting_Haze is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns The multiplier: 1 the real air's haze, 0 none.
 */
static inline float WorldSpriteComponent_GetHaze(const WorldSpriteComponent* self)
{
    return self->_haze;
}

/**
 * @brief Sets this placement's own haze multiplier (it applies while WorldSpriteSetting_Haze is overridden): how hazy the
 *        sprite looks at a distance.
 * @param self The sprite component; must not be NULL.
 * @param haze Finite, in [0, WORLD_SPRITE_MAX_HAZE]: 1 the real air's haze, 0 as if the air were clear.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value outside the
 *          range.
 */
Error WorldSpriteComponent_SetHaze(WorldSpriteComponent* self, float haze);

/**
 * @brief Returns this placement's own draw distance (it applies while WorldSpriteSetting_DrawDistance is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns Metres, above 0.
 */
static inline float WorldSpriteComponent_GetDrawDistance(const WorldSpriteComponent* self)
{
    return self->_drawDistance;
}

/**
 * @brief Sets this placement's own draw distance (it applies while WorldSpriteSetting_DrawDistance is overridden).
 * @param self The sprite component; must not be NULL.
 * @param drawDistance Metres; finite and above 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value that is not
 *          finite or not above 0.
 */
Error WorldSpriteComponent_SetDrawDistance(WorldSpriteComponent* self, float drawDistance);

/**
 * @brief Returns the draw distance multiplier: it multiplies the draw distance in effect (2 = seen twice as far).
 * @param self The sprite component; must not be NULL.
 * @returns The multiplier, above 0 (default 1).
 */
static inline float WorldSpriteComponent_GetDrawDistanceMultiplier(const WorldSpriteComponent* self)
{
    return self->_drawDistanceMultiplier;
}

/**
 * @brief Sets the draw distance multiplier (it multiplies the world sprite's, its kind's or the placement's draw
 *        distance, whichever is in effect).
 * @param self The sprite component; must not be NULL.
 * @param multiplier Finite and above 0; 1 changes nothing.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for a value that is not
 *          finite or not above 0.
 */
Error WorldSpriteComponent_SetDrawDistanceMultiplier(WorldSpriteComponent* self, float multiplier);

/**
 * @brief Returns this placement's own "the quality setting leaves the draw distance as it is" (it applies while
 *        WorldSpriteSetting_IsDrawDistanceFixed is overridden).
 * @param self The sprite component; must not be NULL.
 * @returns true if fixed.
 */
static inline bool WorldSpriteComponent_IsDrawDistanceFixed(const WorldSpriteComponent* self)
{
    return self->_isDrawDistanceFixed;
}

/**
 * @brief Sets this placement's own "the quality setting leaves the draw distance as it is" (it applies while
 *        WorldSpriteSetting_IsDrawDistanceFixed is overridden).
 * @param self The sprite component; must not be NULL.
 * @param isFixed Whether the player's draw_distance_scale leaves the draw distance as it is.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL.
 */
Error WorldSpriteComponent_SetIsDrawDistanceFixed(WorldSpriteComponent* self, bool isFixed);

/**
 * @brief The settings that apply to this placement: each setting from the world sprite, or the placement's own value
 *        while its override flag is on; then the draw distance in effect (the overriding one, else the world sprite's,
 *        else the kind's default for the settings in effect: WorldSpriteSettings_GetDefaultDrawDistance) times the
 *        multiplier.
 *
 * A pure function (the renderer calls it for every sprite each frame).
 * @param self The sprite component; must not be NULL.
 * @param spriteSettings The world sprite's settings (WorldSprite_GetSettings); must not be NULL.
 * @returns The settings in effect. HasDrawDistance is always true and DrawDistance at least 0: +infinity when the
 *          product overflows, 0 when two tiny factors underflow (nothing within reach is drawn then).
 */
WorldSpriteSettings WorldSpriteComponent_ResolveSettings(const WorldSpriteComponent* self,
    const WorldSpriteSettings* spriteSettings);
