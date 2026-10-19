#pragma once
#include <stdbool.h>
#include <stddef.h>
// RenderTexture2D (the frame target the renderer blits its result into) is a Raylib type in this API,
// so the Raylib include is unavoidable here (as in the other rendering headers).
#include "raylib/raylib.h"
#include "wr/WRError.h"


/**
 * @file WorldRenderer.h
 * @brief Renders a World through a GameCamera, owning the render-target pipeline. Separate from world data.
 *
 * The world model carries no rendering logic; the WorldRenderer turns a World + a GameCamera into pixels and
 * never writes to the world or to shared assets. World objects reference models by name; the renderer
 * resolves each object's model once (again only when its name changes) and holds the models under its own
 * asset user. Sprite animations belong to the world (World_Construct2), so the renderer only reads each
 * sprite's current frame.
 *
 * PIPELINE. The world is drawn into an internal floating-point (HDR) "scene" target, then tonemapped into the
 * caller's frame target. With pixelation enabled (default) the scene target is sized to the low pixel
 * resolution (~640 on the long axis, square pixels) and point-upscaled, giving the game's chunky pixel look.
 * In order: the shadow cascades of the sky's shadow light (the sun by day, the moon on bright nights) and the
 * point-light cube shadow; a normal/flag G-buffer and screen-space ambient occlusion (applied to the ambient term
 * only); the scene pass (atmospheric sky with the moon, the moonlit sky, the Milky Way and light pollution's skyglow,
 * the star field, PBR-lit models with the sun, the moon and forward-shaded point lights and distance fog, sprite
 * billboards, debug grid and light gizmos; the sky's dim light and the reflected light go through the eye's night
 * vision, which greys them at night as the eye does);
 * depth/normal-edge outlines; bloom and sun shafts (added in HDR); the tonemap with eye adaptation; and last
 * the crisp overlay, which draws OmitPixelation objects at window resolution over the pixelated image.
 *
 * FAILURES. A missing shader or a GPU target that cannot be created disables the feature that needs it, with
 * one log line when it happens; WorldRenderer_GetStatus reports what ran. The only fatal failure is a scene
 * target that cannot be created at all.
 *
 * The sky's textures (the Milky Way, the moon's face) are the ones the world's environment names, loaded when it names
 * them (PrepareWorld, or the first render after a change) and released when it names others.
 *
 * USAGE. Create one renderer, call WorldRenderer_PrepareWorld once the world is built, then call
 * WorldRenderer_RenderToTarget each frame from a game frame's Render with the frame's target. Objects added
 * later are picked up automatically. The renderer opens and closes all of its own render passes (including
 * the final blit into the frame target), so it must NOT be called inside an already-active render pass.
 *
 * THREADING. All calls are main-thread only (asset manager + Raylib rendering).
 */


// Forward declarations (referenced only by pointer; full types in their own headers).
/** @brief The asset manager; full type in AssetManager.h. Borrowed by the renderer. */
typedef struct AssetManagerStruct AssetManager;
/** @brief The logger; full type in Logger.h. Borrowed; may be NULL. */
typedef struct LoggerStruct Logger;
/** @brief The game config (post-effect config-side multipliers); full type in Config.h. Borrowed; may be NULL. */
typedef struct GameConfigStruct GameConfig;
/** @brief A live world; full type in World.h. */
typedef struct WorldStruct World;
/** @brief The game camera; full type in GameCamera.h. */
typedef struct GameCameraStruct GameCamera;

/** @brief The world renderer. Opaque; construct with WorldRenderer_Create. */
typedef struct WorldRendererStruct WorldRenderer;


// Types.
/** @brief The renderer features WorldRenderer_GetStatus reports on. */
typedef enum WorldRendererFeatureEnum
{
    /** @brief PBR lighting of model objects. Without it models draw unlit, with raylib's default shader. */
    WorldRendererFeature_PbrLighting,
    /** @brief The atmospheric sky. Without it a CPU-computed gradient colour fills the background. */
    WorldRendererFeature_Sky,
    /** @brief The tonemap with eye adaptation. Without it the linear HDR scene is shown untonemapped. */
    WorldRendererFeature_Tonemap,
    /** @brief The sprite shader. Without it sprites draw with raylib's default shader (slightly too bright). */
    WorldRendererFeature_SpriteShading,
    /** @brief The two shadow cascades, cast by the sun (inactive while the moon casts them, or neither). */
    WorldRendererFeature_SunShadows,
    /** @brief The cube shadow of the shadow-casting point light nearest the camera. */
    WorldRendererFeature_PointLightShadow,
    /** @brief Screen-space ambient occlusion of the ambient term. */
    WorldRendererFeature_AmbientOcclusion,
    /** @brief Depth/normal-edge outlines. */
    WorldRendererFeature_Outlines,
    /** @brief HDR bloom. */
    WorldRendererFeature_Bloom,
    /** @brief Sun shafts (god rays). */
    WorldRendererFeature_Sunshafts,
    /** @brief The crisp overlay that draws OmitPixelation objects at window resolution. */
    WorldRendererFeature_CrispOverlay,
    /** @brief The star field (inactive by day, while twilight and light pollution hide every star, or with a star
     *  density of 0). */
    WorldRendererFeature_Stars,
    /** @brief The Milky Way in the sky (unavailable without its texture; inactive while it cannot be seen). */
    WorldRendererFeature_MilkyWay,
    /** @brief The two shadow cascades, cast by the moon (on bright nights, once it is the brighter light and high
     *  enough). */
    WorldRendererFeature_MoonShadows,
    /** @brief The moon's disc in the sky (inactive while it is below the horizon, near the sun, or switched off; drawn
     *  flat grey without its texture). */
    WorldRendererFeature_Moon,
    /** @brief The moon's light on the world (inactive while the moon is down, by day, or without moonlight). */
    WorldRendererFeature_Moonlight,
    /** @brief The eye's night vision: dim reflected and scattered light loses its colour into a faintly blue grey, as
     *  the eye's does when the rods take over (active at dusk and at night; inactive by day, or switched off by the
     *  renderer toggle or the world's NightVisionStrength). */
    WorldRendererFeature_NightVision,
    /** @brief The number of features; not a feature. */
    WorldRendererFeature_Count
} WorldRendererFeature;

/** @brief What a feature did in the most recent render. */
typedef enum WorldRendererFeatureStateEnum
{
    /** @brief The feature ran. */
    WorldRendererFeatureState_Active,
    /** @brief The feature can run but did not: it is switched off (renderer toggle, world or config strength 0),
     *  it does not apply (the sun or the moon is down, nothing to draw), or nothing has been rendered yet. */
    WorldRendererFeatureState_Inactive,
    /** @brief The feature cannot run: a shader it needs failed to load, a GPU target could not be created, or a
     *  feature it builds on is unavailable. The cause was logged once when it happened. */
    WorldRendererFeatureState_Unavailable
} WorldRendererFeatureState;

/**
 * @brief A snapshot of what the renderer did in its most recent render, for diagnostics and debug overlays.
 *
 * Filled by WorldRenderer_GetStatus. A plain value type; nothing to release.
 */
typedef struct WorldRendererStatusStruct
{
    /** @brief Whether the scene target is floating-point (RGBA16F); false means the 8-bit fallback. */
    bool IsSceneHdr;
    /** @brief Whether the scene depth is a samplable texture (AO, outlines, sun shafts and the crisp overlay
     *  need it). */
    bool IsSceneDepthSamplable;
    /** @brief The state of each feature, indexed by WorldRendererFeature. */
    WorldRendererFeatureState Features[WorldRendererFeature_Count];
    /** @brief Model objects with a usable model. */
    size_t ModelCount;
    /** @brief Model objects that draw nothing: no model name, or a model that failed to load (logged once). */
    size_t ModelWithoutAssetCount;
    /** @brief Sprite objects with a frame to draw. */
    size_t SpriteCount;
    /** @brief Point lights in the world. */
    size_t LightCount;
    /** @brief Stars in the star catalogue (from the environment's StarDensity), whether or not they show now. */
    size_t StarCount;
} WorldRendererStatus;


// Functions.
/**
 * @brief Creates a world renderer bound to an asset manager (mints its own asset users).
 * @param assetManager Resolves object assets; borrowed, must outlive the renderer. Must not be NULL.
 * @param logger For asset-resolution diagnostics; borrowed, may be NULL.
 * @param config The game config supplying the config-side post-effect multipliers (shadow/AO/...); borrowed,
 *        must outlive the renderer, may be NULL (then the config-side multipliers default to 1).
 * @param outRenderer [out] Receives the new renderer, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p assetManager or @p outRenderer is NULL.
 * @note May propagate errors from internal calls.
 */
Error WorldRenderer_Create(AssetManager* assetManager, Logger* logger, const GameConfig* config,
    WorldRenderer** outRenderer);

/**
 * @brief Releases the renderer: unloads its render targets, drops asset holds, and retires its asset users.
 *
 * Must be called while the GL context is alive (it unloads GPU render targets). Safe on NULL.
 * @param self The renderer, or NULL.
 * @returns Success, or the first non-success Error encountered while releasing assets.
 * @note May propagate errors from internal calls.
 */
Error WorldRenderer_Deconstruct(WorldRenderer* self);

/**
 * @brief Loads the shaders, the sky textures the world's environment names and the models of the world's objects, so
 *        the first frames do not stall on them.
 *
 * Resolves every model object's model under a fresh asset user and then retires the previous one, so models
 * that no world object uses any more are released (asset holds are not counted, so this swap is how unused
 * models get released). Failures are logged once and skipped, not fatal. Objects added later are resolved by
 * WorldRenderer_RenderToTarget automatically, but a model that failed is not retried until its object is
 * renamed: call this again after asset definitions change (new search roots, AssetManager_ReadDefinitions,
 * AssetManager_ClearLoadFailures), after a large change to release unused models, and whenever a different world
 * is rebuilt at the address of the previous one.
 * @param self The renderer; must not be NULL.
 * @param world The world whose assets to load; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p world is NULL.
 * @note May propagate errors from internal calls.
 */
Error WorldRenderer_PrepareWorld(WorldRenderer* self, World* world);

/**
 * @brief Renders the world from the camera through the full pipeline and blits the result into @p target.
 *
 * Brings the per-object cache up to date (new objects and renamed models are resolved here, failures logged
 * once), draws the 3D world into the internal scene target (recreating the targets if the frame size or the
 * pixelation setting changed), then tonemaps it into @p target (point-upscaled when pixelation is enabled).
 * Opens and closes all its own render passes, so it must NOT be called inside an active render pass.
 * @param self The renderer; must not be NULL.
 * @param world The world to draw; must not be NULL. Read only.
 * @param camera The camera to view from; must not be NULL.
 * @param deltaSeconds Real seconds since the previous render, used to pace HDR eye adaptation (auto-exposure).
 *        Pass the render tick's frame delta; 0, negative or non-finite holds the current adaptation.
 * @param target The frame render target to blit the final image into (its texture size is the window size).
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p world or @p camera is NULL;
 *          ErrorCode_InvalidState if no scene render target can be created at all (the only fatal failure).
 * @note May propagate errors from internal calls.
 */
Error WorldRenderer_RenderToTarget(WorldRenderer* self, World* world, const GameCamera* camera,
    double deltaSeconds, RenderTexture2D target);

/**
 * @brief Reports what the most recent render did: the scene target tier, each feature's state, object counts.
 *
 * Before the first render every feature reads as inactive and the counts are 0.
 * @param self The renderer; must not be NULL.
 * @param outStatus [out] Receives the snapshot. Must not be NULL.
 */
void WorldRenderer_GetStatus(const WorldRenderer* self, WorldRendererStatus* outStatus);

/**
 * @brief Returns a short English name for a feature, for log lines and debug overlays.
 * @param feature The feature.
 * @returns A static, NUL-terminated UTF-8 string (u8"unknown" for a value outside the enum).
 */
const unsigned char* WorldRenderer_GetFeatureName(WorldRendererFeature feature);

/**
 * @brief Logs the status of the most recent render (one info line), e.g. for a debug key.
 *
 * The renderer logs it once by itself after its first render. No-op without a logger.
 * @param self The renderer; must not be NULL.
 */
void WorldRenderer_LogStatus(const WorldRenderer* self);

/**
 * @brief Enables or disables the final pixelation pass (default enabled). Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to render at the low pixel resolution and point-upscale, false to render at full resolution.
 */
void WorldRenderer_SetPixelationEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the pixelation pass is enabled.
 * @param self The renderer; must not be NULL.
 * @returns true if pixelation is enabled.
 */
bool WorldRenderer_IsPixelationEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the whole post-effects pass (screen-space AO + depth/normal-edge outlines).
 *
 * When disabled the scene is blitted straight to the tonemap stage, bit-exact with the pipeline before the
 * post pass existed — useful for A/B-ing whether a visual artifact comes from the post effects. Default
 * enabled. Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to run the post-effects pass, false to bypass it entirely.
 */
void WorldRenderer_SetPostEffectsEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the post-effects pass (screen-space AO + depth/normal-edge outlines) is enabled.
 * @param self The renderer; must not be NULL.
 * @returns true if the post-effects pass runs (given the shaders/targets are available).
 */
bool WorldRenderer_ArePostEffectsEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the eye's night vision: dim reflected and scattered light losing its colour into a faintly
 *        blue grey, as it does for the eye when the rods take over from the cones.
 *
 * A code-side toggle for A/B-ing it; the world's NightVisionStrength scales it further (0 there switches it off for a
 * world). By day it changes nothing either way. Default enabled. Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to apply it, false for colourful nights (what a long-exposure photo shows).
 */
void WorldRenderer_SetNightVisionEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the eye's night vision is enabled (see WorldRenderer_SetNightVisionEnabled).
 * @param self The renderer; must not be NULL.
 * @returns true if enabled.
 */
bool WorldRenderer_IsNightVisionEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the bloom pass (a blurred bright-pass of the scene added back in HDR).
 *
 * A code-side toggle for A/B-ing bloom independently of the AO/outline post pass; when disabled the tonemap
 * adds no bloom. Bloom also self-disables when the effective (world x config) bloom strength is 0 or the bloom
 * shaders/targets are unavailable. Default enabled. Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to run the bloom pass, false to skip it.
 */
void WorldRenderer_SetBloomEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the bloom pass is enabled (the code-side toggle only, not the strength/asset gates).
 * @param self The renderer; must not be NULL.
 * @returns true if the bloom toggle is on.
 */
bool WorldRenderer_IsBloomEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the sun-shaft (god-ray) pass.
 *
 * A code-side toggle for A/B-ing the sun shafts; when disabled the tonemap adds no shafts. Shafts also
 * self-disable when the effective (world x config) strength is 0, the shader/target is unavailable, the scene
 * depth is not samplable, or the sun is below the horizon / off-screen. Default enabled. Applied on the next
 * render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to run the sun-shaft pass, false to skip it.
 */
void WorldRenderer_SetSunshaftsEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the sun-shaft pass is enabled (the code-side toggle only, not the strength/sun gates).
 * @param self The renderer; must not be NULL.
 * @returns true if the sun-shaft toggle is on.
 */
bool WorldRenderer_AreSunshaftsEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the crisp overlay for OmitPixelation objects.
 *
 * When enabled (the default), objects flagged OmitPixelation are drawn un-pixelated at full resolution and
 * composited over the pixelated frame (depth-tested so they are occluded correctly) — e.g. a readable in-world
 * screen. When disabled, those objects fall back to rendering pixelated with the rest of the world (useful for
 * A/B-ing the effect). The overlay also self-disables when its shader/target is unavailable or the world has no
 * flagged objects. Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to draw flagged objects crisply, false to render them pixelated.
 */
void WorldRenderer_SetCrispOverlayEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the crisp overlay is enabled (the code-side toggle only, not the shader/object gates).
 * @param self The renderer; must not be NULL.
 * @returns true if the crisp overlay toggle is on.
 */
bool WorldRenderer_IsCrispOverlayEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the debug light gizmos (wireframe spheres drawn at each light's position).
 *
 * These mark otherwise-invisible light positions for authoring; a real game should turn them OFF so lights
 * have no visible sphere. Independent of the debug grid. Default enabled. Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to draw the light gizmos, false to hide them.
 */
void WorldRenderer_SetLightGizmosEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the debug light gizmos are drawn.
 * @param self The renderer; must not be NULL.
 * @returns true if the light gizmos are drawn.
 */
bool WorldRenderer_AreLightGizmosEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the debug reference grid drawn on the ground plane.
 * @param self The renderer; must not be NULL.
 * @param enabled true to draw the grid, false to hide it.
 */
void WorldRenderer_SetDebugGridEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the debug reference grid is enabled.
 * @param self The renderer; must not be NULL.
 * @returns true if the grid is drawn.
 */
bool WorldRenderer_IsDebugGridEnabled(const WorldRenderer* self);
