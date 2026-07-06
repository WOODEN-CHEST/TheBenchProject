#pragma once
#include <stdbool.h>
// RenderTexture2D (the frame target the renderer blits its result into) is a Raylib type in this API,
// so the Raylib include is unavoidable here (as in the other rendering headers).
#include "raylib/raylib.h"
#include "wr/WRError.h"


/**
 * @file WorldRenderer.h
 * @brief Renders a World through a GameCamera, owning the render-target pipeline. Separate from world data.
 *
 * The world model carries no rendering logic; the WorldRenderer turns a World + a GameCamera into pixels.
 * World objects reference assets by name; the renderer resolves them through the asset manager and holds
 * them alive under its own asset user, releasing them when destroyed.
 *
 * PIPELINE. The renderer owns an internal "scene" render target that it draws the 3D world into, then
 * blits that up into the caller's frame target. When pixelation is enabled (default), the scene target is
 * sized to the low pixel resolution (~640 on the long axis, square pixels) and point-upscaled to the frame
 * target, giving the game's chunky pixel look — this is the "pixel pass" and it runs last. When disabled,
 * the scene target matches the frame target (no pixelation). The richer pipeline layers in: physically based
 * sky/sun, PBR, a sun shadow map (crisp, hard-edged), and a low-resolution post pass that applies screen-space
 * ambient occlusion and 1px outlines (depth/surface-edge silhouettes darken, view-normal-edge creases are
 * sun-aware, in the style of the three.js RenderPixelatedPass) before the tonemap upscale. The sky is never
 * outlined. Light culling, bloom, sunshafts, and fog are later work; sprite objects, lights, and the
 * per-object "omit pixelation" toggle are not handled yet.
 *
 * USAGE. Create one renderer, call WorldRenderer_PrepareWorld once the objects are set, then call
 * WorldRenderer_RenderToTarget each frame from a game frame's Render with the frame's target. The renderer
 * opens and closes all of its own render passes (including the final blit into the frame target), so it
 * must NOT be called inside an already-active render pass.
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


// Functions.
/**
 * @brief Creates a world renderer bound to an asset manager (mints its own asset user).
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
 * @brief Releases the renderer: unloads its render targets, drops asset holds, and retires its asset user.
 *
 * Must be called while the GL context is alive (it unloads GPU render targets). Safe on NULL.
 * @param self The renderer, or NULL.
 * @returns Success, or the first non-success Error encountered while releasing assets.
 * @note May propagate errors from internal calls.
 */
Error WorldRenderer_Deconstruct(WorldRenderer* self);

/**
 * @brief Loads the assets referenced by the world's objects, so rendering does not stall on first use.
 *
 * Resolves each renderable object's asset once (attributed to the renderer's user). Failures are logged and
 * skipped, not fatal. Safe to call again after the world's objects change.
 * @param self The renderer; must not be NULL.
 * @param world The world whose assets to load; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p world is NULL.
 * @note May propagate errors from internal calls.
 */
Error WorldRenderer_PrepareWorld(WorldRenderer* self, World* world);

/**
 * @brief Renders the world from the camera through the full pipeline and blits the result into @p target.
 *
 * Draws the 3D world into the renderer's internal scene target (recreating it if the frame size or
 * pixelation setting changed), then blits it into @p target (point-upscaled when pixelation is enabled).
 * Opens and closes all its own render passes, so it must NOT be called inside an active render pass.
 * Missing/failed object assets are skipped silently (they are reported by WorldRenderer_PrepareWorld).
 * @param self The renderer; must not be NULL.
 * @param world The world to draw; must not be NULL.
 * @param camera The camera to view from; must not be NULL.
 * @param deltaSeconds Real seconds since the previous render, used to pace HDR eye adaptation (auto-exposure).
 *        Pass the render tick's frame delta; 0 (or negative) holds the current adaptation.
 * @param target The frame render target to blit the final image into (its texture size is the window size).
 * @returns Success; ErrorCode_IllegalArgument if @p self, @p world or @p camera is NULL.
 */
Error WorldRenderer_RenderToTarget(WorldRenderer* self, World* world, const GameCamera* camera,
    double deltaSeconds, RenderTexture2D target);

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
