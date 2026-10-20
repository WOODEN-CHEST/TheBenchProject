#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "WorldRenderCache.h"
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): what the world renderer draws this frame (frustum culling, draw distances, fades and
 * opacity), as pure maths and per-frame lists, tested headless (tests/WorldVisibilityTests.c).
 *
 * FRUSTUMS. A view-projection matrix (raymath's order, MatrixMultiply(view, projection); perspective or orthographic)
 * gives six planes (Gribb and Hartmann) with unit normals pointing inward; a sphere is outside when it lies wholly behind
 * one of them. The camera's frustum culls what the scene, G-buffer and crisp passes draw. Shadow casters are never culled
 * by the camera (a caster off screen can still shadow what is on it): each shadow cascade culls its casters against its
 * own orthographic box, and the cube shadow keeps its reach test.
 *
 * DRAW DISTANCE. Every entry has its own (decision G17): a model's is its world model's (T10), a sprite's its world
 * sprite's or its kind's default (T26: sprites 150 m, decals 60 m), or the placement's own while that overrides it,
 * times the placement's multiplier (the render cache resolves it); then times the config's draw_distance_scale unless
 * it is fixed (WorldVisibility_ScaleDrawDistance). An entry's distance is the camera's distance to the nearest point of
 * its bounding sphere (0 inside it), so a large object does not fade while its near end is close, multiplied by the
 * zoom factor (WorldVisibility_GetZoomFactor: tan(fovY/2) / tan(30 degrees), 1 at the default 60-degree field of view,
 * below 1 zoomed in), so a zoomed view (binoculars, a camera) shows far objects instead of fading them. T11's LOD
 * selection uses the same factor. Beyond its draw distance an entry is not drawn; over the last stretch (10% of the
 * distance, at least 3 m, at most all of it) its distance fade rises linearly from 0 to 1.
 *
 * FADES. A draw's fade runs from 0 (solid) to 1 (gone): the larger of its distance fade and 1 - its opacity (the tint's).
 * Models and cutout sprites show it as a dither; blended and additive sprites, transparent already, take their distance
 * fade into their alpha (their opacity is in it already) and do not dither. An entry whose fade is 1 is in no list. The
 * G-buffer draws an entry solid while its fade is below WORLD_VISIBILITY_GBUFFER_FADE_LIMIT and skips it above, so
 * outlines and AO switch off once, at mid-fade, instead of breaking up with the dither.
 *
 * THE DITHER. shaders/common/dither.glsl keeps a share of a draw's pixels by a 4x4 ordered (Bayer) pattern on the scene's
 * pixel grid: cell = floor(gl_FragCoord.xy / ditherPixelScale) mod 4, threshold = (BAYER[cell.y][cell.x] + 0.5) / 16. A
 * pixel is kept when both masks keep it: the draw's fade (threshold > fade) and its part of a LOD cross-fade (T11: the
 * outgoing LOD keeps threshold > progress, the incoming one threshold <= progress, so the two keep complementary pixels;
 * a LOD drawn alone has progress 0 and keeps every pixel). WorldVisibility_GetDitherCell,
 * WorldVisibility_GetDitherThreshold and WorldVisibility_IsDitherDiscarded are that formula in C, for the tests.
 *
 * LISTS. WorldVisibility_Build fills, from the render cache, one list per pass (WorldVisibilityPass) of the entries it
 * draws, as indices into the cache's models and sprites, plus every entry's fades and the counts the renderer's status
 * reports. The lists are rebuilt every frame and grow with the world; once grown, a build allocates nothing.
 */


// Macros.
/** @brief The vertical field of view, in degrees, that draw and LOD distances are authored at: the zoom factor is 1
 *  there (the camera's default). */
#define WORLD_VISIBILITY_REFERENCE_FOV_Y 60.0
/** @brief The fade band: this share of the draw distance, but at least WORLD_VISIBILITY_MIN_FADE_BAND metres (and at
 *  most the whole distance). */
#define WORLD_VISIBILITY_FADE_BAND_SHARE 0.1f
#define WORLD_VISIBILITY_MIN_FADE_BAND 3.0f
/** @brief The G-buffer draws an entry solid while its fade is below this and skips it from this fade on. */
#define WORLD_VISIBILITY_GBUFFER_FADE_LIMIT 0.5f
/** @brief The dither pattern's side, in pixels, and its number of thresholds (WORLD_VISIBILITY_DITHER_SIZE squared). */
#define WORLD_VISIBILITY_DITHER_SIZE 4U
#define WORLD_VISIBILITY_DITHER_LEVELS 16U


// Types.
/** @brief A plane: the points p with dot(Normal, p) + Distance = 0. Normal is unit and points to the inside. */
typedef struct WorldPlaneStruct
{
    /** @brief Unit normal, toward the inside of the volume the plane bounds. */
    Vector3 Normal;
    /** @brief Signed offset: dot(Normal, p) + Distance is p's signed distance from the plane (positive inside). */
    float Distance;
} WorldPlane;

/** @brief The planes of a view volume, in the order WorldVisibility_ExtractFrustum extracts them. */
typedef enum WorldFrustumPlaneEnum
{
    /** @brief Clip x = -w. */
    WorldFrustumPlane_Left,
    /** @brief Clip x = +w. */
    WorldFrustumPlane_Right,
    /** @brief Clip y = -w. */
    WorldFrustumPlane_Bottom,
    /** @brief Clip y = +w. */
    WorldFrustumPlane_Top,
    /** @brief Clip z = -w: the near plane. */
    WorldFrustumPlane_Near,
    /** @brief Clip z = +w: the far plane. */
    WorldFrustumPlane_Far,
    /** @brief The number of planes; not a plane. */
    WorldFrustumPlane_Count
} WorldFrustumPlane;

/** @brief A view volume (a perspective frustum or an orthographic box) as six inward planes. A plain value type. */
typedef struct WorldFrustumStruct
{
    /** @brief The planes, indexed by WorldFrustumPlane. */
    WorldPlane Planes[WorldFrustumPlane_Count];
} WorldFrustum;

/** @brief The passes the lists are built for. */
typedef enum WorldVisibilityPassEnum
{
    /** @brief The camera's passes: the scene, the G-buffer and the crisp overlay. */
    WorldVisibilityPass_Camera,
    /** @brief The near shadow cascade's casters. */
    WorldVisibilityPass_NearCascade,
    /** @brief The far shadow cascade's casters. */
    WorldVisibilityPass_FarCascade,
    /** @brief The point-light cube shadow's casters. */
    WorldVisibilityPass_PointShadow,
    /** @brief The number of passes; not a pass. */
    WorldVisibilityPass_Count
} WorldVisibilityPass;

/** @brief One entry's fades this frame. A plain value type. */
typedef struct WorldVisibilityFadeStruct
{
    /** @brief How far its draw distance fades it: 0 before the fade band .. 1 at the draw distance and beyond. */
    float Distance;
    /** @brief The draw's fade: max(Distance, 1 - opacity). Models and cutout sprites dither by it; 1 = not drawn. */
    float Combined;
} WorldVisibilityFade;

/**
 * @brief What a build is for: the camera, the shadow passes that run this frame, and the settings.
 *
 * A plain value type the caller fills completely. The frustums of passes that do not run are ignored.
 */
typedef struct WorldVisibilityViewStruct
{
    /** @brief The camera's world position (distances are measured from it). */
    Vector3 CameraPosition;
    /** @brief The camera's frustum. */
    WorldFrustum CameraFrustum;
    /** @brief Whether the camera's frustum and the cascades' boxes cull (false: an A/B toggle that keeps everything the
     *  draw distances and opacities leave, as before culling existed; the cube's reach test stays). */
    bool IsCullingEnabled;
    /** @brief Whether the near cascade runs, and its box. */
    bool IsNearCascadeActive;
    WorldFrustum NearCascade;
    /** @brief Whether the far cascade runs, and its box. */
    bool IsFarCascadeActive;
    WorldFrustum FarCascade;
    /** @brief Whether the cube shadow runs, its light's position and its reach (casters must touch the reach sphere). */
    bool IsPointShadowActive;
    Vector3 PointShadowPosition;
    float PointShadowReach;
    /** @brief The draw distance scale (the config's draw_distance_scale; finite and >= 0). */
    float DrawDistanceScale;
    /** @brief The camera's zoom factor (WorldVisibility_GetZoomFactor): the entries' distances are multiplied by it
     *  before they are compared with their draw distances. Anything not finite or not above 0 counts as 1. */
    float ZoomFactor;
} WorldVisibilityView;

/** @brief A build's counts, for the renderer's status. A plain value type. */
typedef struct WorldVisibilityCountsStruct
{
    /** @brief Models and sprites the camera's passes draw. */
    size_t DrawnModelCount;
    size_t DrawnSpriteCount;
    /** @brief Models and sprites within their draw distance, not fully transparent, but outside the camera's view. */
    size_t OutsideViewCount;
    /** @brief Models and sprites beyond their draw distance or fully transparent (their fade is 1). */
    size_t FadedOutCount;
    /** @brief Of the drawn ones, those fading or translucent (fade above 0): dithered, or alpha-faded (blended and
     *  additive sprites). */
    size_t FadingCount;
    /** @brief Of the drawn models, how many show each LOD ([i] counts LOD i): the LOD they show, or fade to. */
    size_t DrawnModelsByLod[WORLD_MODEL_MAX_LODS];
    /** @brief Of the drawn models, those cross-fading between two LODs. */
    size_t LodFadingCount;
} WorldVisibilityCounts;

/**
 * @brief One frame's visibility: per pass, the cache entries it draws; per entry, its fades.
 *
 * Construct with WorldVisibility_Construct and release with WorldVisibility_Deconstruct; fill with WorldVisibility_Build.
 * Fields are internal to WorldVisibility.c: read through the accessors.
 */
typedef struct WorldVisibilityStruct
{
    /** @brief WorldVisibilityFade per cache model, in the cache's order. */
    GenericBuffer _modelFades;
    /** @brief WorldVisibilityFade per cache sprite, in the cache's order. */
    GenericBuffer _spriteFades;
    /** @brief Per pass, the indices (size_t, ascending) of the cache models it draws. */
    GenericBuffer _models[WorldVisibilityPass_Count];
    /** @brief Per pass, the indices (size_t, ascending) of the cache sprites it draws. */
    GenericBuffer _sprites[WorldVisibilityPass_Count];
    /** @brief The last build's counts. */
    WorldVisibilityCounts _counts;
    /** @brief Whether the camera's list holds an OmitPixelation (crisp) model or sprite. */
    bool _hasCrispObjects;
} WorldVisibility;


// Functions: frustums.
/**
 * @brief Extracts the six planes of a view volume from its view-projection matrix (Gribb and Hartmann; OpenGL clip
 *        space, -w <= x, y, z <= w), normalized, normals inward.
 * @param viewProjection World -> clip, in raymath's order (MatrixMultiply(view, projection)); perspective or
 *        orthographic.
 * @returns The frustum. Constant time.
 */
WorldFrustum WorldVisibility_ExtractFrustum(Matrix viewProjection);

/**
 * @brief Whether a sphere touches a frustum: false only when it lies wholly behind one of the planes.
 *
 * Conservative near the frustum's corners (a sphere just outside a corner can count as inside), never the other way.
 * A sphere with a NaN in its centre or radius counts as outside.
 * @param frustum The frustum; must not be NULL.
 * @param center The sphere's centre.
 * @param radius Its radius (>= 0).
 * @returns true when the sphere may be inside.
 */
bool WorldVisibility_IsSphereInFrustum(const WorldFrustum* frustum, Vector3 center, float radius);


// Functions: draw distances and fades.
/**
 * @brief An entry's own draw distance under the player's quality setting: times the scale, unless it is fixed.
 * @param drawDistance The entry's draw distance, in metres (its world model's or world sprite's, its kind's or its
 *        placement's, times its multiplier).
 * @param isFixed Whether the quality setting leaves it as it is (landmarks, gameplay-critical things).
 * @param scale The draw distance scale; anything not finite or below 0 counts as 1.
 * @returns The draw distance to compare with.
 */
float WorldVisibility_ScaleDrawDistance(float drawDistance, bool isFixed, float scale);

/**
 * @brief The zoom factor of a field of view: tan(fovY / 2) / tan(WORLD_VISIBILITY_REFERENCE_FOV_Y / 2).
 *
 * Distances are multiplied by it before they are compared with draw distances (and, from T11, LOD switch distances):
 * 1 at the reference 60 degrees (exactly), below 1 when zoomed in, so a narrower view keeps far objects visible and
 * detailed, and above 1 for a wider one.
 * @param fovYDegrees The camera's vertical field of view, in degrees.
 * @returns The factor (above 0); 1 for a field of view that is not finite or not in (0, 180).
 */
float WorldVisibility_GetZoomFactor(float fovYDegrees);

/**
 * @brief The fade band of a draw distance: WORLD_VISIBILITY_FADE_BAND_SHARE of it, at least
 *        WORLD_VISIBILITY_MIN_FADE_BAND metres, at most all of it.
 * @param drawDistance The draw distance.
 * @returns The band's length (0 for a draw distance that is not above 0).
 */
float WorldVisibility_GetFadeBand(float drawDistance);

/**
 * @brief How far an entry's draw distance fades it: 0 up to the start of the fade band, rising linearly to 1 at the
 *        draw distance, 1 beyond it.
 * @param distance The entry's distance (WorldVisibility_GetEntryDistance); a NaN counts as beyond.
 * @param drawDistance Its draw distance; 0 or below fades everything out, +infinity nothing.
 * @returns The distance fade, in [0, 1]; monotonic in @p distance.
 */
float WorldVisibility_GetDistanceFade(float distance, float drawDistance);

/**
 * @brief The camera's distance to the nearest point of a bounding sphere (0 when the camera is inside it).
 * @param cameraPosition The camera's position.
 * @param center The sphere's centre.
 * @param radius Its radius.
 * @returns The distance (>= 0, or NaN for non-finite input).
 */
float WorldVisibility_GetEntryDistance(Vector3 cameraPosition, Vector3 center, float radius);

/**
 * @brief The fade an opacity asks for: 1 - opacity, the opacity clamped to [0, 1] (a NaN counts as 0: transparent).
 * @param opacity The opacity (the tint's).
 * @returns The fade, in [0, 1].
 */
float WorldVisibility_GetOpacityFade(float opacity);

/**
 * @brief A draw's fade from its distance fade and its opacity fade: the larger of the two.
 * @param distanceFade The distance fade (WorldVisibility_GetDistanceFade).
 * @param opacityFade The opacity fade (WorldVisibility_GetOpacityFade).
 * @returns The combined fade.
 */
float WorldVisibility_CombineFades(float distanceFade, float opacityFade);


// Functions: the dither (the C mirror of shaders/common/dither.glsl).
/**
 * @brief The dither cell coordinate of a fragment coordinate: floor(fragCoord / max(pixelScale, 1)), the scene pixel a
 *        pass's pixel lies in. In the crisp overlay (pixelScale = window / scene) every window pixel of one scene pixel
 *        gets that scene pixel's cell, so the pattern sits on the scene's pixel grid.
 * @param fragCoord gl_FragCoord.x or .y (a pixel centre is index + 0.5); must be >= 0.
 * @param pixelScale The pass's pixels per scene pixel (ditherPixelScale's component).
 * @returns The cell coordinate (take it mod WORLD_VISIBILITY_DITHER_SIZE for the pattern).
 */
uint32_t WorldVisibility_GetDitherCell(float fragCoord, float pixelScale);

/**
 * @brief The dither threshold of a cell: (BAYER[y mod 4][x mod 4] + 0.5) / 16, with y counted from the target's
 *        bottom row (gl_FragCoord's origin).
 * @param x The cell's column.
 * @param y The cell's row.
 * @returns One of the 16 thresholds (k + 0.5) / 16, k = 0..15; each appears once in every 4x4 block.
 */
float WorldVisibility_GetDitherThreshold(uint32_t x, uint32_t y);

/**
 * @brief Whether the dither discards a cell's pixel: the pixel is kept only when its threshold is above the draw's fade
 *        and its part of a LOD cross-fade keeps it (the outgoing LOD where the threshold is above the progress, the
 *        incoming LOD where it is at or below it). A fade of k/16 keeps 16 - k of every 16 pixels; at any progress the
 *        outgoing and the incoming LOD keep complementary pixels, and the fade removes its pixels from both.
 * @param fade The draw's fade (0 keeps every pixel, 1 none).
 * @param lodProgress The LOD cross-fade's progress: the share of the pixels the incoming LOD takes (0 for a draw that is
 *        not part of a cross-fade).
 * @param isIncomingLod Whether the draw is the incoming LOD of a cross-fade.
 * @param x The cell's column.
 * @param y The cell's row.
 * @returns true when the pixel is discarded.
 */
bool WorldVisibility_IsDitherDiscarded(float fade, float lodProgress, bool isIncomingLod, uint32_t x, uint32_t y);


// Functions: the lists.
/**
 * @brief Initializes empty lists.
 * @param self The visibility; must not be NULL.
 */
void WorldVisibility_Construct(WorldVisibility* self);

/**
 * @brief Releases the lists' buffers.
 * @param self The visibility, or NULL.
 */
void WorldVisibility_Deconstruct(WorldVisibility* self);

/**
 * @brief Rebuilds the lists for this frame from the render cache.
 *
 * Every entry gets its fades (WorldVisibilityFade; entries the passes do not draw at all get 1), from its own draw
 * distance (WorldVisibility_ScaleDrawDistance) against its zoomed distance. An entry with a combined fade below 1
 * joins:
 *   - the camera's list when it is drawn (a model must be IsDrawn) and inside the camera's frustum;
 *   - a cascade's list when it casts shadows (a model with CastsShadows whose shadow tier routes it to that cascade; a
 *     sprite with WorldRenderSprite_CastsShadows), that cascade runs and its sphere is inside the cascade's box;
 *   - the cube shadow's list when it casts shadows, the cube shadow runs and its sphere touches the light's reach sphere.
 * A model's sphere is its world bounding sphere; a sprite's is the sphere around its pivot that holds its quad in every
 * orientation (WorldRenderSprite.BoundsRadius), so one sphere serves the camera and the lights. With culling off the
 * camera's frustum and the cascades' boxes cull nothing. Main thread only; no allocation once the lists have grown.
 * @param self The visibility; must not be NULL.
 * @param cache The render cache, synced for this frame; must not be NULL. Its entries are read, never written.
 * @param view What the lists are for; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument; ErrorCode_InvalidOperation if a list could not grow
 *          (the lists are then empty).
 */
Error WorldVisibility_Build(WorldVisibility* self, const WorldRenderCache* cache, const WorldVisibilityView* view);

/**
 * @brief Returns a pass's model list: indices into @p cache's models, ascending.
 * @param self The visibility; must not be NULL.
 * @param cache The cache the lists were built from; must not be NULL. When its model count is not the one the lists were
 *        built for (no build since its last sync), the list is empty: an index can never run past the cache.
 * @param pass The pass.
 * @param outCount [out] Receives the entry count. Must not be NULL.
 * @returns The indices (borrowed until the next build); NULL when there are none.
 */
const size_t* WorldVisibility_GetModels(const WorldVisibility* self, const WorldRenderCache* cache,
    WorldVisibilityPass pass, size_t* outCount);

/**
 * @brief Returns a pass's sprite list: indices into @p cache's sprites, ascending (see WorldVisibility_GetModels).
 * @param self The visibility; must not be NULL.
 * @param cache The cache the lists were built from; must not be NULL.
 * @param pass The pass.
 * @param outCount [out] Receives the entry count. Must not be NULL.
 * @returns The indices (borrowed until the next build); NULL when there are none.
 */
const size_t* WorldVisibility_GetSprites(const WorldVisibility* self, const WorldRenderCache* cache,
    WorldVisibilityPass pass, size_t* outCount);

/**
 * @brief Returns a cache model's fades this frame.
 * @param self The visibility; must not be NULL.
 * @param index The model's index in the cache (from a list).
 * @returns Its fades; fully faded (1, 1) for an index the last build did not cover.
 */
WorldVisibilityFade WorldVisibility_GetModelFade(const WorldVisibility* self, size_t index);

/**
 * @brief Returns a cache sprite's fades this frame.
 * @param self The visibility; must not be NULL.
 * @param index The sprite's index in the cache (from a list).
 * @returns Its fades; fully faded (1, 1) for an index the last build did not cover.
 */
WorldVisibilityFade WorldVisibility_GetSpriteFade(const WorldVisibility* self, size_t index);

/**
 * @brief Returns the last build's counts.
 * @param self The visibility; must not be NULL.
 * @returns The counts (all 0 before the first build).
 */
static inline WorldVisibilityCounts WorldVisibility_GetCounts(const WorldVisibility* self)
{
    return self->_counts;
}

/**
 * @brief Reports whether the camera's list holds an OmitPixelation (crisp) model or sprite: the crisp overlay's input.
 * @param self The visibility; must not be NULL.
 * @returns true if the camera's passes draw at least one crisp entry.
 */
static inline bool WorldVisibility_HasCrispObjects(const WorldVisibility* self)
{
    return self->_hasCrispObjects;
}
