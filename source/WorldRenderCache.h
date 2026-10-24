#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "AssetManager.h"
#include "GameModel.h"
#include "World.h"
#include "WorldAnimatorComponent.h"
#include "WorldModel.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "WorldLightComponent.h"
#include "WorldLod.h"
#include "SpriteAnimation.h"
#include "SpriteTransform.h"
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): the world renderer's per-object render cache.
 *
 * At the start of every render, WorldRenderCache_Sync walks the world's per-type component arrays once
 * (World_GetComponents) and builds the lists every pass shares: the model components (with their resolved world model,
 * the LOD drawn, the variant, the settings in effect, final transform and world bounding sphere), the sprites that have
 * a frame to draw (with the settings in effect: their world sprite's, or the placement's own while their override flags
 * are on, WorldSpriteComponent_ResolveSettings, taken every frame), and the point lights. Passes iterate these lists
 * instead of walking the world and resolving assets by name themselves. Every placement is the object's world one,
 * through its parents (WorldObject_GetWorldMatrix,
 * WorldObject_GetWorldTransform, WorldObject_GetWorldPosition), read without writing to the world.
 *
 * RENDER SPACE (T32, WorldRenderSpace.h). Each entry keeps its world placement in double (WorldMatrix and Center,
 * Position) and, for the frame the lists are synced for, the same placement in render space: relative to the frame's
 * origin (the camera's position), subtracted in double and rounded to float once (RenderMatrix, RenderCenter, the
 * sprites' Shape and FixedTransform, RenderPosition). The passes, the visibility and the LOD selection read only the
 * render-space values, so this sync is the one place a world position becomes a float the GPU sees. The world stores
 * its positions and world matrices in double (T33), which this sync reads as they are.
 *
 * Objects that are disabled or hidden, or under a disabled or hidden ancestor (WorldObject_IsEffectivelyEnabled,
 * WorldObject_IsEffectivelyVisible), are not drawn: their model entries stay (keeping the resolved model) but are not
 * IsDrawn, and their sprites and lights are not listed.
 *
 * World models (WorldModel.h) are resolved once per component: an entry remembers the component's revision and the
 * hash of the name it resolved; when the revision changes the name is hashed again, and the world model is resolved
 * again only if the name changed. A failed resolution is logged once and not retried until the name changes. The
 * variant is resolved to an index with the world model, and again when the revision changes and the component's
 * uses_variant or variant name did; an unknown name draws the model's own materials and is logged once. The settings in
 * effect (the world model's, or the placement's own while its override flag is on: WorldModelComponent_ResolveSettings)
 * are taken every frame. Entries follow the world's model components across additions and removals: when the world's
 * component-set version changes, entries are re-matched by owner id, and one is carried over only if its owner still
 * names the world model it was resolved for (an id can come back with another object). A different world resets the
 * cache; the per-frame walk also checks each entry's component, so a world rebuilt at the same address is caught as soon
 * as a component differs.
 *
 * POSES (T29). An entry whose object has an animator posed for the entry's world model (the animator's pose names that
 * asset: WorldAnimatorComponent_GetPose) takes the pose (Pose): the passes skin its skinned meshes with it, and unless
 * the world model gives bounds of its own its bounding sphere is the pose's box, through the skeleton LOD's import
 * transform, instead of LOD 0's bind pose (WorldModel_HasOwnBounds). A pose made for another world model (the animator
 * not yet rebound after a change) is not taken.
 *
 * LODS (T11). Each model entry keeps its LOD state across frames (WorldLod.h: the two LODs of a cross-fade, the fade's
 * progress and direction); WorldRenderCache_UpdateLods, once per frame after the sync, picks every drawn entry's LOD by
 * the camera's distance and moves its cross-fade on. A LOD is drawn with its own import transform inside the object's
 * render-space matrix (WorldRenderModel_GetLodTransform); the bounding sphere is the world model's, the same for every
 * LOD.
 *
 * The cache only reads the world, its objects and components, and never writes to an asset. Every pointer in the lists
 * is borrowed and valid until the world changes; the world models (and with them their LOD models and textures) are
 * held under the asset user passed to Sync, the sprites' world sprites (and their animations) under the world's own,
 * which their components bind.
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
    /** @brief Hash of the world model name the entry was resolved for (0 for none); meaningful once IsResolved is
     *  true. Re-matching uses it to tell an object that reuses an id from the object the entry was made for. */
    uint64_t NameHash;
    /** @brief The resolved world model, held under the model user passed to Sync; NULL when the component names none or
     *  it failed to load (then the object draws nothing). Its LOD models are borrowed from it. Read-only. */
    const WorldModel* Asset;
    // TODO: one LOD state per entry serves one camera. A second view of the same world through this renderer (a mirror,
    // a camera feed on the CRT) needs a state per view, or the fades would follow whichever camera updated last.
    /** @brief The LOD drawn and the cross-fade (WorldLod.h), kept across frames: picked by WorldRenderCache_UpdateLods,
     *  started afresh when the world model is resolved again or the entry was not drawn. */
    WorldLodState Lod;
    /** @brief Whether the placement's own LOD distances were refused last frame (its list does not fit the world model):
     *  the warning is logged when this turns true. */
    bool IsLodListRefused;
    /** @brief The component's uses_variant and the hash of its variant name when the variant was resolved (0 for none),
     *  so a change of either resolves it again. */
    bool UsesVariant;
    uint64_t VariantNameHash;
    /** @brief Whether the entry draws a variant (uses_variant on and the world model has a variant of that name), and
     *  which (WorldModel_ApplyVariant). */
    bool HasVariant;
    size_t VariantIndex;
    /** @brief Centre and radius of the world model's bounding sphere, and the transform from its space to the object's
     *  (WorldModel_GetBoundsTransform), in the object's own space; valid when Asset is not NULL. */
    Vector3 LocalCenter;
    float LocalRadius;
    Matrix BoundsTransform;
    /** @brief Whether the passes draw the entry this frame: its world model resolved, its object (effectively) enabled
     *  and visible. */
    bool IsDrawn;
    /** @brief The render settings in effect (this frame): the world model's, or the component's own while their
     *  override flags are on (WorldModelComponent_ResolveSettings). */
    bool HasOutline;
    bool OmitsPixelation;
    WorldShadowTier ShadowTier;
    bool CastsShadows;
    /** @brief The draw distance in effect, in metres, times the component's multiplier (>= 0: +infinity never fades, 0
     *  is never drawn), and whether the player's draw_distance_scale leaves it as it is (this frame). WorldVisibility
     *  applies the scale and the zoom. */
    float DrawDistance;
    bool IsDrawDistanceFixed;
    /** @brief The object's world matrix (this frame), in double. Valid when Asset is not NULL. */
    Matrix4Double WorldMatrix;
    /** @brief World-space bounding sphere (this frame), its centre in double: conservative under rotation, non-uniform
     *  scale and the shear a hierarchy can add (WorldTransform_GetMaxScale). Valid when Asset is not NULL. */
    Vector3Double Center;
    float Radius;
    /** @brief The world matrix and the sphere's centre in render space (this frame): relative to the origin the lists
     *  were synced for, subtracted in double and rounded once. Each LOD is drawn with its own import transform inside
     *  RenderMatrix (WorldRenderModel_GetLodTransform). Valid when Asset is not NULL. */
    Matrix RenderMatrix;
    Vector3 RenderCenter;
    /** @brief The object's final tint colour (this frame). */
    Color Tint;
    /** @brief The object's animator's pose (this frame; borrowed from the animator, valid until the animation system's
     *  next update) when it was made for this entry's world model, or NULL: no animator, no pose, or another world model
     *  (T29). */
    const WorldAnimatorPose* Pose;
    /** @brief The pose the camera's passes draw (T30): Pose, except for the first-person object
     *  (WorldRenderCache_SetFirstPersonObject), whose world model's first_person block hides bones: a copy of its pose
     *  (the cache's) with their skinning collapsed to their joint, so the camera riding its eyes does not see them. Its
     *  shadows keep Pose. */
    const WorldAnimatorPose* CameraPose;
} WorldRenderModel;

/** @brief A sprite component that has a frame to draw this frame, with everything the passes need. Its settings are the
 *  ones in effect (WorldSpriteComponent_ResolveSettings: its world sprite's, or the placement's own while their
 *  override flags are on). */
typedef struct WorldRenderSpriteStruct
{
    /** @brief The sprite component (borrowed); valid until the world changes. */
    const WorldSpriteComponent* Component;
    /** @brief The current frame of its playback instance (the texture is borrowed from the animation). */
    SpriteAnimationFrame Frame;
    /** @brief The frame's area in its texture, normalized: x, y of its top left, width, height (the shader's uvRect). */
    float UvRect[4];
    /** @brief The pivot's world position (the object's world position), in double. */
    Vector3Double Position;
    /** @brief The quad before a viewer turns it (source/SpriteTransform.h), in render space: orientation, the pivot's
     *  position relative to the origin the lists were synced for (subtracted in double, rounded once), the object's
     *  world axes, the world size (the frame's pixels / pixels per unit x the object's world X and Y scale), the pivot,
     *  the flips. */
    SpriteShape Shape;
    /** @brief The fixed orientation's matrix in render space (it does not depend on the viewer); valid when
     *  Shape.Orientation is WorldSpriteOrientation_Fixed. */
    Matrix FixedTransform;
    /** @brief Radius of a sphere around the pivot that holds the quad in every orientation (light culling, the cube
     *  shadow's reach). */
    float BoundsRadius;
    /** @brief The object's final tint colour (its alpha is the object's opacity). */
    Color Tint;
    /** @brief The alpha mode, cutout threshold and lighting. */
    WorldSpriteAlphaMode AlphaMode;
    float AlphaCutoff;
    bool IsLit;
    float Emission;
    float LightWrap;
    /** @brief Whether it is drawn as a decal (marked as one, with the fixed orientation), and how far it is lifted. */
    bool IsDecal;
    float DecalOffset;
    /** @brief Whether it casts shadows (only cutout sprites that are not decals cast). */
    bool CastsShadows;
    /** @brief The outline setting. */
    bool HasOutline;
    /** @brief Whether it is drawn crisp: omit_pixelation on a cutout sprite that is not a decal (the crisp overlay
     *  composites opaque surfaces only, so the others stay pixelated). */
    bool OmitsPixelation;
    /** @brief The draw distance in effect, in metres, times the component's multiplier (>= 0: +infinity never fades, 0
     *  is never drawn), and whether the player's draw_distance_scale leaves it as it is (this frame). WorldVisibility
     *  applies the scale and the zoom. */
    float DrawDistance;
    bool IsDrawDistanceFixed;
} WorldRenderSprite;

/** @brief A point light as the passes use it this frame. */
typedef struct WorldRenderLightStruct
{
    /** @brief The light component (borrowed); valid until the world changes. */
    const WorldLightComponent* Component;
    /** @brief World-space position (the object's world position), in double. */
    Vector3Double Position;
    /** @brief The position in render space: relative to the origin the lists were synced for (subtracted in double,
     *  rounded once). */
    Vector3 RenderPosition;
    /** @brief Reach radius (the light's range). */
    float Range;
    /** @brief Emission intensity. */
    float Intensity;
    /** @brief The light's sRGB colour (alpha ignored). */
    Color Color;
    /** @brief Whether the light may cast a shadow. */
    bool CastsShadows;
} WorldRenderLight;

/** @brief What a frame's LOD update needs (WorldRenderCache_UpdateLods): the camera, the quality setting and the frame's
 *  time. A plain value type. */
typedef struct WorldRenderLodViewStruct
{
    /** @brief The camera's position in render space (the space of the entries' RenderCenter: (0, 0, 0) for the camera
     *  whose position the lists were synced for): an object's LOD distance is measured from it to its bounding sphere's
     *  centre. */
    Vector3 CameraPosition;
    /** @brief The camera's zoom factor (WorldVisibility_GetZoomFactor): the distances are multiplied by it, so zooming in
     *  picks finer LODs. Anything not finite or not above 0 counts as 1. */
    float ZoomFactor;
    /** @brief The player's lod_distance_scale: it multiplies every switch distance (above 1: more detail). Anything not
     *  finite or below 0 counts as 1. */
    float DistanceScale;
    /** @brief Real seconds since the previous frame: the cross-fades advance by them (0 holds them). */
    float DeltaSeconds;
} WorldRenderLodView;

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
    /** @brief The object the camera rides (T30; 0: none), and the copy of its pose its camera passes draw, with its own
     *  palette (_firstPersonPaletteBones bones of room; owned). */
    uint64_t _firstPersonObjectId;
    WorldAnimatorPose _firstPersonPose;
    float* _firstPersonPalette;
    uint32_t _firstPersonPaletteBones;
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
 * @brief Names the object the camera rides (T30: the player in first person), or none (0): from the next Sync, its
 *        entry's CameraPose leaves out the bones its world model's first_person block hides.
 * @param self The cache; must not be NULL.
 * @param objectId The object's id, or 0.
 */
static inline void WorldRenderCache_SetFirstPersonObject(WorldRenderCache* self, uint64_t objectId)
{
    self->_firstPersonObjectId = objectId;
}

/**
 * @brief Forgets every entry, so the next Sync resolves every model again.
 *
 * Call it before the asset user the entries were resolved under is released (their asset pointers would dangle)
 * and when a different world may live at the same address as the previous one.
 * @param self The cache; must not be NULL.
 */
void WorldRenderCache_Reset(WorldRenderCache* self);

/**
 * @brief Brings the cache up to date with @p world and rebuilds this frame's lists, in render space for @p origin.
 *
 * One walk over the world's model, sprite and light components. Every entry gets its world placement (in double) and
 * the same placement relative to @p origin (subtracted in double, rounded to float once): the render-space values the
 * passes draw with. A world other than the last one resets the cache;
 * entries are re-matched to the model components (by owner id and world model name) when the component-set version
 * changed since the last Sync, or when an entry no longer belongs to the component at its position. A world model whose
 * entry is new or whose name changed is resolved with AssetManager_LoadWorldModel under @p modelUser; a failure is
 * logged once (warning) and leaves the entry without an asset until the name changes. Variants are resolved with the
 * world model and when the component's variant changes (an unknown one logged once). Every model's settings, transform
 * and bounds are recomputed. Main thread only; resolving a world model loads its LOD models, which needs the GL
 * context.
 * @param self The cache; must not be NULL.
 * @param world The world to draw; must not be NULL.
 * @param origin The frame's origin, render space's (0, 0, 0): the camera's world position (any origin while only the
 *        world models matter, as when preparing a world).
 * @param assets Resolves the world models; borrowed. Must not be NULL.
 * @param modelUser The asset user the world models are held under; must outlive the entries (see Reset).
 * @param logger Receives one warning per failed world model resolution and per unknown variant; borrowed, may be
 *        NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p world or @p assets is NULL;
 *          ErrorCode_InvalidOperation if a list could not grow (the lists are then incomplete).
 */
Error WorldRenderCache_Sync(WorldRenderCache* self, World* world, Vector3Double origin, AssetManager* assets,
    AssetUserID modelUser, Logger* logger);

/**
 * @brief Picks the LOD every drawn model entry shows this frame and advances its cross-fade (WorldLod.h). Call it once
 *        per frame, after WorldRenderCache_Sync.
 *
 * For each entry that is drawn: its LOD settings in effect (WorldModelComponent_ResolveLodSettings; when the placement's
 * own list does not fit its world model, the world model's distances, with one warning when that starts), the camera's
 * distance to its bounding sphere's centre times the zoom factor, WorldLod_Select from the LOD it heads to, then
 * WorldLodState_Update with the frame's seconds. An entry that is not drawn (no world model, a disabled or hidden object)
 * starts afresh when it is drawn again, so it appears at its LOD without a fade. Entries beyond their draw distance or
 * outside the view are still updated: their shadows use their LOD. Distances are measured in render space (the entries'
 * RenderCenter from the view's CameraPosition). Main thread only; never writes to the world.
 * @param self The cache, synced for this frame; must not be NULL.
 * @param view The camera and the frame; must not be NULL.
 * @param logger Receives the warning when an object's own LOD distances stop fitting its world model; borrowed, may be
 *        NULL.
 */
void WorldRenderCache_UpdateLods(WorldRenderCache* self, const WorldRenderLodView* view, Logger* logger);

/**
 * @brief Returns one LOD's model of a cached model entry.
 * @param model The entry; must not be NULL and must have an asset.
 * @param lod The LOD, 0 the finest.
 * @returns The model (borrowed from the world model), or NULL for a LOD past the last.
 */
static inline GameModel* WorldRenderModel_GetLodModel(const WorldRenderModel* model, size_t lod)
{
    return WorldModel_GetLodModel(model->Asset, lod);
}

/**
 * @brief Returns the final transform one LOD of a cached model entry is drawn with this frame, in render space: the LOD's
 *        import transform inside the object's render-space matrix (raylib's DrawModelEx convention).
 * @param model The entry; must not be NULL and must have an asset.
 * @param lod The LOD, 0 the finest; past the last LOD the render-space matrix alone.
 * @returns The transform.
 */
Matrix WorldRenderModel_GetLodTransform(const WorldRenderModel* model, size_t lod);

/**
 * @brief Returns this frame's model entries: one per model component, in world order, including ones that are not
 *        drawn (no world model, or a disabled or hidden object): the passes draw only the IsDrawn ones.
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
 * @brief Returns this frame's drawable sprites (with a frame to draw, on effectively enabled and visible objects).
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
 * @brief Returns this frame's point lights: every light component on an effectively enabled and visible object,
 *        including ones without intensity or reach.
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
