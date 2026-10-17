#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "AssetManager.h"
#include "GameModel.h"
#include "World.h"
#include "WorldModelObject.h"
#include "WorldSpriteObject.h"
#include "WorldLight.h"
#include "SpriteAnimation.h"
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): the world renderer's per-object render cache.
 *
 * At the start of every render, WorldRenderCache_Sync walks the world once and builds the lists every pass
 * shares: the model objects (with their resolved model asset, final transform and world bounding sphere), the
 * sprites that have a frame to draw, and the point lights. Passes iterate these lists instead of walking the
 * world and resolving assets by name themselves.
 *
 * Model assets are resolved once per object: an entry remembers the object's asset revision and is resolved
 * again only when the revision changes (the model was renamed). A failed resolution is logged once and not
 * retried until then. Entries follow the world's objects across additions and removals: when the world's
 * object-set version changes, entries are re-matched by object id, and one is carried over only if its object
 * still names the model it was resolved for (an id can be reused by a different object). A different world
 * resets the cache; the per-frame walk also checks each entry's object, so a world rebuilt at the same address
 * is caught as soon as an object differs.
 *
 * The cache only reads the world and its objects, and never writes to an asset. Every pointer in the lists is
 * borrowed and valid until the world changes; the models are held under the asset user passed to Sync.
 */


// Types.
/** @brief A model object as the passes draw it. */
typedef struct WorldRenderModelStruct
{
    /** @brief Id of the model object this entry belongs to. */
    uint64_t ObjectId;
    /** @brief The object's asset revision the asset was resolved for; meaningful once IsResolved is true. */
    uint32_t AssetRevision;
    /** @brief Whether a resolution was attempted for AssetRevision (it succeeded, or it failed and was logged). */
    bool IsResolved;
    /** @brief Hash of the model name the entry was resolved for (0 for none); meaningful once IsResolved is true.
     *  Re-matching uses it to tell an object that reuses an id from the object the entry was made for. */
    uint64_t NameHash;
    /** @brief The resolved model, held under the model user passed to Sync; NULL when the object has no asset name
     *  or its model failed to load (then the object draws nothing). Read-only. */
    GameModel* Asset;
    /** @brief Centre and radius of the asset's mesh-space bounding sphere; valid when Asset is not NULL. */
    Vector3 LocalCenter;
    float LocalRadius;
    /** @brief The model object (borrowed); valid until the world changes. */
    WorldModelObject* Object;
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

/** @brief A sprite object that has a frame to draw this frame. */
typedef struct WorldRenderSpriteStruct
{
    /** @brief The sprite object (borrowed); valid until the world changes. */
    WorldSpriteObject* Object;
    /** @brief The current frame of its playback instance (the texture is borrowed from the animation). */
    SpriteAnimationFrame Frame;
    /** @brief World-space centre of the billboard. */
    Vector3 Position;
    /** @brief Billboard width and height in world units (the object's X and Y scale). */
    Vector2 Size;
    /** @brief The object's final tint colour. */
    Color Tint;
} WorldRenderSprite;

/** @brief A point light as the passes use it this frame. */
typedef struct WorldRenderLightStruct
{
    /** @brief The light (borrowed); valid until the world changes. */
    const WorldLight* Light;
    /** @brief World-space position. */
    Vector3 Position;
    /** @brief Reach radius (the light's size). */
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
    /** @brief WorldRenderModel entries, one per model object, in the world's object order. */
    GenericBuffer _models;
    /** @brief Scratch WorldRenderModel buffer used while re-matching entries to a changed object list. */
    GenericBuffer _rematch;
    /** @brief WorldRenderSprite entries of this frame. */
    GenericBuffer _sprites;
    /** @brief WorldRenderLight entries of this frame, in the world's object order. */
    GenericBuffer _lights;
    /** @brief The world the model entries were matched against (compared by address only; never dereferenced). */
    const World* _world;
    /** @brief That world's object-set version when the entries were matched. */
    uint64_t _objectSetVersion;
    /** @brief Whether the entries were matched at least once since construction or the last reset. */
    bool _isMatched;
    /** @brief Whether this frame has a drawable OmitPixelation (crisp) model or sprite. */
    bool _hasCrispObjects;
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
 * One walk over the world's objects. A world other than the last one resets the cache; entries are re-matched to
 * the objects (by id and model name) when the object-set version changed since the last Sync, or when an entry no
 * longer belongs to the object at its position. A model whose entry is new or whose asset revision changed is
 * resolved with AssetManager_LoadModel under @p modelUser; a failure is logged once (warning) and leaves the
 * entry without an asset until the next rename. Every model's transform and bounds are recomputed. Main thread
 * only; resolving a model needs the GL context.
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
 * @brief Returns this frame's model entries (one per model object, in world order, including ones without asset).
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
 * @brief Returns this frame's drawable sprites.
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
 * @brief Returns this frame's point lights (every light object, including ones without intensity or reach).
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
 * @brief Reports whether this frame has a drawable OmitPixelation model or sprite (the crisp overlay's input).
 * @param self The cache; must not be NULL.
 * @returns true if at least one model with a resolved asset, or one listed sprite, is flagged OmitPixelation.
 */
static inline bool WorldRenderCache_HasCrispObjects(const WorldRenderCache* self)
{
    return self->_hasCrispObjects;
}
