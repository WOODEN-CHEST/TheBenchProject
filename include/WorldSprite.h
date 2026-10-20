#pragma once
#include <stdbool.h>
#include <stddef.h>
// raylib.h is needed for Vector2 (the pivot); PropertyDescriptor.h for the enums' names (PropertyEnumTable) and the
// settings' property tables.
#include "raylib/raylib.h"
#include "PropertyDescriptor.h"

/** @brief A sprite animation asset (SpriteAnimation.h); a world sprite plays one, loaded as its dependency. */
typedef struct SpriteAnimationStruct SpriteAnimation;


/**
 * @file WorldSprite.h
 * @brief The world sprite: how a sprite animation stands, is lit and is seen in the 3D world, kept apart from its
 *        placements.
 *
 * A world sprite is an asset of the "world_sprite" type (JSON definitions in a resource pack's world_sprites/ folder,
 * references/asset_structure.md; load one with AssetManager_LoadWorldSprite). It names the sprite animation to play and
 * holds the defaults of its placements (WorldSpriteSettings): the shape (orientation, pivot, pixels per unit, flips),
 * the transparency, the lighting, whether it is a decal, the render flags and the draw distance. A sprite animation
 * stays a plain asset that text components use too, where these settings mean nothing; what only matters in the world
 * lives here (the owner's decision G17), as world models do for models (WorldModel.h).
 *
 * A placed object names its world sprite in its sprite component (WorldSpriteComponent.h) and may override EVERY
 * setting with an explicit flag next to its own value; WorldSpriteComponent_ResolveSettings turns the two into the
 * settings that apply. Changing the component's name swaps the world sprite and keeps the object.
 *
 * ONE COPY OF THE RULES. The settings are described by two property tables (WorldSprite_GetSettingsPropertyTable for
 * the definition's root keys, WorldSprite_GetRenderPropertyTable for its "render" block), and the sprite component's
 * table is built from the same rows: a setting has one key, default, rule and description wherever it is stored. The
 * two tables together describe every field of WorldSpriteSettings (copy, compare and write with both).
 *
 * DRAW DISTANCE. A world sprite gives its draw distance or leaves it to its kind (has_draw_distance off): a sprite is
 * then seen WORLD_SPRITE_DEFAULT_DRAW_DISTANCE metres, a decal WORLD_SPRITE_DEFAULT_DECAL_DRAW_DISTANCE. The kind is
 * decided per placement, after its overrides, since a placement may turn a sprite into a decal. A definition that gives
 * draw_distance without has_draw_distance has it on (the hand-written form); a writer that leaves out default values
 * must still write has_draw_distance whenever it writes draw_distance, or a value kept while the flag is off would read
 * back as on (PropertyWriteOptions.AreDefaultsWritten writes it).
 *
 * OWNERSHIP. The asset is pure data. Its sprite animation is loaded as a DEPENDENCY, under the world sprite's dependency
 * user: holding the world sprite holds the animation, and it unloads right after it. The asset manager owns the world
 * sprite; never keep its pointer past your asset user (AssetManager.md).
 *
 * Every field of the loaded asset is read-only (underscore-prefixed): read them through the accessors.
 */


// Macros.
/** @brief The default pixels per world unit (metre): a 64x32 image is 2 by 1 metres. */
#define WORLD_SPRITE_DEFAULT_PIXELS_PER_UNIT 32.0f
/** @brief The default cutout threshold: cutout sprites do not draw texels less opaque than it. */
#define WORLD_SPRITE_DEFAULT_ALPHA_CUTOFF 0.5f
/** @brief The default lift of a decal off its surface, in metres. */
#define WORLD_SPRITE_DEFAULT_DECAL_OFFSET 0.01f
/** @brief How far a sprite that is not a decal is seen when neither its world sprite nor its placement gives a draw
 *  distance, in metres, before the player's draw_distance_scale. */
#define WORLD_SPRITE_DEFAULT_DRAW_DISTANCE 150.0f
/** @brief How far a decal is seen when neither its world sprite nor its placement gives a draw distance, in metres,
 *  before the player's draw_distance_scale. */
#define WORLD_SPRITE_DEFAULT_DECAL_DRAW_DISTANCE 60.0f


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
 * @brief The render flags of a sprite: the "render" block of a world sprite's definition (as a world model's).
 *
 * A plain value type, described by WorldSprite_GetRenderPropertyTable.
 */
typedef struct WorldSpriteRenderSettingsStruct
{
    /** @brief Whether the renderer draws a 1-pixel outline around the sprite (cutout sprites that are not decals). */
    bool HasOutline;
    /** @brief Whether the sprite is drawn at full resolution over the pixelated image (cutout sprites that are not
     *  decals). */
    bool OmitsPixelation;
    /** @brief Whether the sprite casts shadows (cutout sprites that are not decals; it receives them either way). */
    bool CastsShadows;
} WorldSpriteRenderSettings;

/**
 * @brief Everything a placed sprite can override: as a world sprite sets it (the defaults of its placements), or as it
 *        applies to one placement (WorldSpriteComponent_ResolveSettings).
 *
 * A plain value type. WorldSprite_GetSettingsPropertyTable describes every field but Render, which
 * WorldSprite_GetRenderPropertyTable describes. There are no cross-field rules: a combination that does not apply (a
 * "decal" billboard, crisp smoke) is kept as set and simply has no effect.
 */
typedef struct WorldSpriteSettingsStruct
{
    /** @brief How the quad is turned. */
    WorldSpriteOrientation Orientation;
    /** @brief The point of the image at the object's position, in image units: (0, 0) bottom left, (1, 1) top right;
     *  finite, may lie outside the image. */
    Vector2 Pivot;
    /** @brief Image pixels per world unit (> 0): the world size is the frame's size in pixels divided by this, times the
     *  object's X and Y scale. */
    float PixelsPerUnit;
    /** @brief Whether the image is mirrored left to right, about the pivot. */
    bool IsFlippedX;
    /** @brief Whether the image is mirrored top to bottom, about the pivot. */
    bool IsFlippedY;
    /** @brief How transparent texels are drawn. */
    WorldSpriteAlphaMode AlphaMode;
    /** @brief The cutout threshold, in [0, 1]. */
    float AlphaCutoff;
    /** @brief Whether the sprite is lit like the models around it; false shows its colours as emitted light (neon,
     *  fire), still fogged. */
    bool IsLit;
    /** @brief Brightness multiplier of an unlit sprite's light (>= 0); unused while lit. */
    float Emission;
    /** @brief How far light wraps around a lit billboard or axis billboard, in [0, 1]: 0 a flat card facing the camera,
     *  1 a round thing (half lit from the side). Fixed sprites are flat cards. */
    float LightWrap;
    /** @brief Whether the sprite is a decal lying on a surface (drawn as one only with the fixed orientation:
     *  WorldSpriteSettings_IsDrawnAsDecal). */
    bool IsDecal;
    /** @brief Metres a decal is pushed off its surface along its normal, toward the side it is seen from (>= 0). */
    float DecalOffset;
    /** @brief The render flags (the definition's "render" block). */
    WorldSpriteRenderSettings Render;
    /** @brief Whether DrawDistance applies (the row has_draw_distance). Off, the placements get their kind's default
     *  (WorldSpriteSettings_GetDefaultDrawDistance) after their overrides, and DrawDistance is kept unused. The settings
     *  that apply to a placement always have one. */
    bool HasDrawDistance;
    /** @brief How far the sprite is seen, in metres, before the player's draw_distance_scale and the zoom: above 0 in a
     *  world sprite (the rules); once resolved for a placement, the product with its multiplier: +infinity when it
     *  overflows (never faded by distance), 0 when two tiny factors underflow (nothing within reach is drawn).
     *  Meaningful while HasDrawDistance is true. */
    float DrawDistance;
    /** @brief Whether the player's draw_distance_scale leaves the draw distance as it is (landmarks, gameplay-critical
     *  signs). */
    bool IsDrawDistanceFixed;
} WorldSpriteSettings;

/**
 * @brief A loaded world sprite. Built and owned by the asset manager (the "world_sprite" type); read-only.
 */
typedef struct WorldSpriteStruct
{
    /** @brief The sprite animation it plays, held under the world sprite's dependency user (valid while the world
     *  sprite is). */
    SpriteAnimation* _animation;
    /** @brief The defaults of its placements. */
    WorldSpriteSettings _settings;
} WorldSprite;


// Fields.
/** @brief The names of WorldSpriteOrientation ("billboard", "axis_billboard", "fixed"). */
extern const PropertyEnumTable WORLD_SPRITE_ORIENTATION_ENUM;
/** @brief The names of WorldSpriteAlphaMode ("cutout", "blend", "additive"). */
extern const PropertyEnumTable WORLD_SPRITE_ALPHA_MODE_ENUM;


// Functions.
/**
 * @brief Returns the property table of a world sprite's settings ("world_sprite"): the keys of its definition's root,
 *        describing every field of WorldSpriteSettings but Render.
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldSprite_GetSettingsPropertyTable(void);

/**
 * @brief Returns the property table of a world sprite's render flags ("world_sprite_render"): the keys of its
 *        definition's "render" block, describing WorldSpriteRenderSettings.
 * @returns The table (static, immutable).
 */
const PropertyTable* WorldSprite_GetRenderPropertyTable(void);

/**
 * @brief The settings a world sprite and a placement start from: the tables' defaults (a centred, camera-facing,
 *        lit cutout sprite at WORLD_SPRITE_DEFAULT_PIXELS_PER_UNIT, outlined, casting shadows) and no draw distance of
 *        its own (the kind's default applies).
 * @returns The settings.
 */
WorldSpriteSettings WorldSpriteSettings_CreateDefault(void);

/**
 * @brief Whether settings draw the sprite as a decal: marked as one and with the fixed orientation.
 * @param self The settings; must not be NULL.
 * @returns true for a decal.
 */
static inline bool WorldSpriteSettings_IsDrawnAsDecal(const WorldSpriteSettings* self)
{
    return self->IsDecal && (self->Orientation == WorldSpriteOrientation_Fixed);
}

/**
 * @brief The draw distance of the sprite's kind, for settings that give none: WORLD_SPRITE_DEFAULT_DECAL_DRAW_DISTANCE
 *        for a decal (WorldSpriteSettings_IsDrawnAsDecal), WORLD_SPRITE_DEFAULT_DRAW_DISTANCE otherwise.
 * @param self The settings; must not be NULL.
 * @returns Metres, before the player's draw_distance_scale.
 */
static inline float WorldSpriteSettings_GetDefaultDrawDistance(const WorldSpriteSettings* self)
{
    return WorldSpriteSettings_IsDrawnAsDecal(self) ? WORLD_SPRITE_DEFAULT_DECAL_DRAW_DISTANCE
        : WORLD_SPRITE_DEFAULT_DRAW_DISTANCE;
}

/**
 * @brief Returns the sprite animation the world sprite plays.
 * @param self The world sprite; must not be NULL.
 * @returns The animation (borrowed: valid while the world sprite is loaded).
 */
static inline SpriteAnimation* WorldSprite_GetAnimation(const WorldSprite* self)
{
    return self->_animation;
}

/**
 * @brief Returns the defaults of the world sprite's placements.
 * @param self The world sprite; must not be NULL.
 * @returns The settings (borrowed: valid while the world sprite is loaded).
 */
static inline const WorldSpriteSettings* WorldSprite_GetSettings(const WorldSprite* self)
{
    return &self->_settings;
}
