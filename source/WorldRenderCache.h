#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "AssetManager.h"
#include "GameModel.h"
#include "World.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "WorldLightComponent.h"
#include "SpriteAnimation.h"
#include "SpriteTransform.h"
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): the world renderer's per-object render cache.
 *
 * At the start of every render, WorldRenderCache_Sync walks the world's per-type component arrays once
 * (World_GetComponents) and builds the lists every pass shares: the model components (with their resolved model asset,
 * final transform and world bounding sphere), the sprites that have a frame to draw, and the point lights. Passes
 * iterate these lists instead of walking the world and resolving assets by name themselves.
 *
 * Objects that are disabled or hidden (WorldObject_IsEnabled, WorldObject_IsVisible) are not drawn: their model
 * entries stay (keeping the resolved model) but are not IsDrawn, and their sprites and lights are not listed.
 *
 * Model assets are resolved once per component: an entry remembers the component's revision and the hash of the name
 * it resolved; when the revision changes the name is hashed again, and the model is resolved again only if the name
 * changed. A failed resolution is logged once and not retried until the name changes. Entries follow the world's model
 * components across additions and removals: when the world's component-set version changes, entries are re-matched by
 * owner id, and one is carried over only if its owner still names the model it was resolved for (an id can come back
 * with another object). A different world resets the cache; the per-frame walk also checks each entry's component,
 * so a world rebuilt at the same address is caught as soon as a component differs.
 *
 * The cache only reads the world, its objects and components, and never writes to an asset. Every pointer in the lists
 * is borrowed and valid until the world changes; the models are held under the asset user passed to Sync.
 */


// Types.
/** @brief A model component as the passes draw it. */
typedef struct WorldRenderModelStruct
{
    /** @brief Id of the object that carries the component. */
    uint64_t ObjectId;
    /** @brief The model component (borrowed); valid until the world changes. */
    const WorldModelComponent* Component;
    /** @brief The component revision the entry last checked; meaningful once IsResolved is true. */
    uint32_t Revision;
    /** @brief Whether a resolution was attempted for the current name (it succeeded, or it failed and was logged). */
    bool IsResolved;
    /** @brief Hash of the model name the entry was resolved for (0 for none); meaningful once IsResolved is true.
     *  Re-matching uses it to tell an object that reuses an id from the object the entry was made for. */
    uint64_t NameHash;
    /** @brief The resolved model, held under the model user passed to Sync; NULL when the component names no model or
     *  its model failed to load (then the object draws nothing). Read-only. */
    GameModel* Asset;
    /** @brief Centre and radius of the asset's mesh-space bounding sphere; valid when Asset is not NULL. */
    Vector3 LocalCenter;
    float LocalRadius;
    /** @brief Whether the passes draw the entry this frame: its model resolved, its object enabled and visible. */
    bool IsDrawn;
    /** @brief The component's render settings (this frame). */
    bool HasOutline;
    bool OmitsPixelation;
    WorldShadowTier ShadowTier;
    bool CastsShadows;
    /** @brief The asset's import transform composed with the object's world transform (this frame). Valid when
     *  Asset is not NULL. */
    Matrix Transform;
    /** @brief World-space bounding sphere (this frame): conservative under rotation and non-uniform scale. Valid
     *  when Asset is not NULL. */
    Vector3 Center;
    float Radius;
    /** @brief The object's final tint colour (this frame). */
    Color Tint;
} WorldRenderModel;

/** @brief A sprite component that has a frame to draw this frame, with everything the passes need. */
typedef struct WorldRenderSpriteStruct
{
    /** @brief The sprite component (borrowed); valid until the world changes. */
    const WorldSpriteComponent* Component;
    /** @brief The current frame of its playback instance (the texture is borrowed from the animation). */
    SpriteAnimationFrame Frame;
    /** @brief The frame's area in its texture, normalized: x, y of its top left, width, height (the shader's uvRect). */
    float UvRect[4];
    /** @brief The quad before a viewer turns it (source/SpriteTransform.h): orientation, the object's position and
     *  axes, the world size (the frame's pixels / pixels per unit x the object's X and Y scale), the pivot, the flips. */
    SpriteShape Shape;
    /** @brief The fixed orientation's matrix (it does not depend on the viewer); valid when Shape.Orientation is
     *  WorldSpriteOrientation_Fixed. */
    Matrix FixedTransform;
    /** @brief Radius of a sphere around Shape.Position that holds the quad in every orientation (light culling, the
     *  cube shadow's reach). */
    float BoundsRadius;
    /** @brief The object's final tint colour (its alpha is the object's opacity). */
    Color Tint;
    /** @brief The component's alpha mode, cutout threshold and lighting. */
    WorldSpriteAlphaMode AlphaMode;
    float AlphaCutoff;
    bool IsLit;
    float Emission;
    float LightWrap;
    /** @brief Whether it is drawn as a decal (marked as one, with the fixed orientation), and how far it is lifted. */
    bool IsDecal;
    float DecalOffset;
    /** @brief The component's casts_shadows (only cutout sprites that are not decals cast). */
    bool CastsShadows;
    /** @brief The component's outline setting. */
    bool HasOutline;
    /** @brief Whether it is drawn crisp: omit_pixelation on a cutout sprite that is not a decal (the crisp overlay
     *  composites opaque surfaces only, so the others stay pixelated). */
    bool OmitsPixelation;
} WorldRenderSprite;

/** @brief A point light as the passes use it this frame. */
typedef struct WorldRenderLightStruct
{
    /** @brief The light component (borrowed); valid until the world changes. */
    const WorldLightComponent* Component;
    /** @brief World-space position (the object's). */
    Vector3 Position;
    /** @brief Reach radius (the light's range). */
    float Range;
    /** @brief Emission intensity. */
    float Intensity;
    /** @brief The light's sRGB colour (alpha ignored). */
    Color Color;
    /** @brief Whether the light may cast a shadow. */
    bool CastsShadows;
} WorldRenderLight;

/**
 * @brief The per-object render cache and this frame's lists.
 *
 * Construct with WorldRenderCache_Construct and release with WorldRenderCache_Deconstruct. Fields are internal
 * to WorldRenderCache.c; read the lists through the accessors.
 */
typedef struct WorldRenderCacheStruct
{
    /** @brief WorldRenderModel entries, one per model component, in the world's component order. */
    GenericBuffer _models;
    /** @brief Scratch WorldRenderModel buffer used while re-matching entries to a changed component list. */
    GenericBuffer _rematch;
    /** @brief WorldRenderSprite entries of this frame. */
    GenericBuffer _sprites;
    /** @brief WorldRenderLight entries of this frame, in the world's component order. */
    GenericBuffer _lights;
    /** @brief The world the model entries were matched against (compared by address only; never dereferenced). */
    const World* _world;
    /** @brief That world's component-set version when the entries were matched. */
    uint64_t _componentSetVersion;
    /** @brief Whether the entries were matched at least once since construction or the last reset. */
    bool _isMatched;
} WorldRenderCache;


// Functions.
/**
 * @brief Initializes an empty cache.
 * @param self The cache; must not be NULL.
 */
void WorldRenderCache_Construct(WorldRenderCache* self);

/**
 * @brief Releases the cache's buffers. Holds nothing on assets itself: the model user passed to Sync does.
 * @param self The cache, or NULL.
 */
void WorldRenderCache_Deconstruct(WorldRenderCache* self);

/**
 * @brief Forgets every entry, so the next Sync resolves every model again.
 *
 * Call it before the asset user the entries were resolved under is released (their asset pointers would dangle)
 * and when a different world may live at the same address as the previous one.
 * @param self The cache; must not be NULL.
 */
void WorldRenderCache_Reset(WorldRenderCache* self);

/**
 * @brief Brings the cache up to date with @p world and rebuilds this frame's lists.
 *
 * One walk over the world's model, sprite and light components. A world other than the last one resets the cache;
 * entries are re-matched to the model components (by owner id and model name) when the component-set version changed
 * since the last Sync, or when an entry no longer belongs to the component at its position. A model whose entry is new
 * or whose name changed is resolved with AssetManager_LoadModel under @p modelUser; a failure is logged once (warning)
 * and leaves the entry without an asset until the name changes. Every model's transform and bounds are recomputed.
 * Main thread only; resolving a model needs the GL context.
 * @param self The cache; must not be NULL.
 * @param world The world to draw; must not be NULL.
 * @param assets Resolves the models; borrowed. Must not be NULL.
 * @param modelUser The asset user the models are held under; must outlive the entries (see Reset).
 * @param logger Receives one warning per failed model resolution; borrowed, may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p world or @p assets is NULL;
 *          ErrorCode_InvalidOperation if a list could not grow (the lists are then incomplete).
 */
Error WorldRenderCache_Sync(WorldRenderCache* self, World* world, AssetManager* assets, AssetUserID modelUser,
    Logger* logger);

/**
 * @brief Returns this frame's model entries: one per model component, in world order, including ones that are not
 *        drawn (no asset, or a disabled or hidden object): the passes draw only the IsDrawn ones.
 * @param self The cache; must not be NULL.
 * @param outCount [out] Receives the entry count. Must not be NULL.
 * @returns The entries (borrowed until the next Sync or Reset); NULL when there are none.
 */
static inline const WorldRenderModel* WorldRenderCache_GetModels(const WorldRenderCache* self, size_t* outCount)
{
    const void* Entries = self->_models._data;
    *outCount = self->_models._count;
    return Entries;
}

/**
 * @brief Returns this frame's drawable sprites (with a frame to draw, on enabled and visible objects).
 * @param self The cache; must not be NULL.
 * @param outCount [out] Receives the entry count. Must not be NULL.
 * @returns The entries (borrowed until the next Sync or Reset); NULL when there are none.
 */
static inline const WorldRenderSprite* WorldRenderCache_GetSprites(const WorldRenderCache* self, size_t* outCount)
{
    const void* Entries = self->_sprites._data;
    *outCount = self->_sprites._count;
    return Entries;
}

/**
 * @brief Returns this frame's point lights: every light component on an enabled and visible object, including ones
 *        without intensity or reach.
 * @param self The cache; must not be NULL.
 * @param outCount [out] Receives the entry count. Must not be NULL.
 * @returns The entries (borrowed until the next Sync or Reset); NULL when there are none.
 */
static inline const WorldRenderLight* WorldRenderCache_GetLights(const WorldRenderCache* self, size_t* outCount)
{
    const void* Entries = self->_lights._data;
    *outCount = self->_lights._count;
    return Entries;
}

/**
 * @brief Whether a cached sprite is an opaque surface: a cutout sprite that is not a decal. Such a sprite writes depth,
 *        is drawn into the G-buffer (it gets outlines and AO) and may cast shadows; decals and blended or additive
 *        sprites do none of that.
 * @param sprite The sprite; must not be NULL.
 * @returns true for an opaque surface.
 */
static inline bool WorldRenderSprite_IsOpaqueSurface(const WorldRenderSprite* sprite)
{
    return !sprite->IsDecal && (sprite->AlphaMode == WorldSpriteAlphaMode_Cutout);
}

/**
 * @brief Whether a cached sprite casts shadows: an opaque surface (WorldRenderSprite_IsOpaqueSurface) whose component
 *        casts them.
 * @param sprite The sprite; must not be NULL.
 * @returns true for a shadow caster.
 */
static inline bool WorldRenderSprite_CastsShadows(const WorldRenderSprite* sprite)
{
    return sprite->CastsShadows && WorldRenderSprite_IsOpaqueSurface(sprite);
}
