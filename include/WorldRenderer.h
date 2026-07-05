#pragma once
#include <stdbool.h>
#include "wr/WRError.h"


/**
 * @file WorldRenderer.h
 * @brief Renders a World through a GameCamera. Kept fully separate from the world data.
 *
 * The world model (World and its objects) carries no rendering logic; the WorldRenderer is the piece that
 * turns a World + a GameCamera into pixels. World objects reference their assets (models, sprite
 * animations) by name; the renderer resolves those through the asset manager and holds them alive under
 * its own asset user, releasing them when it is destroyed.
 *
 * SCOPE (this stage). This is the minimal, foundational renderer: it draws the world's 3D model objects,
 * lit by their materials, into a caller-provided render pass, and can draw a debug reference grid so the
 * world is navigable before any content exists. The larger pipeline the game targets — the physically
 * based sky and sun, PBR lighting, shadows, per-light culling, bloom, sunshafts, fog, outlines and the
 * pixelation pass — layers onto this renderer in later work; sprite objects and lights are not drawn yet.
 *
 * USAGE. Create one renderer, call WorldRenderer_PrepareWorld once the world's objects are set (it loads
 * the referenced assets, logging any that fail), then call WorldRenderer_Render each frame INSIDE an
 * active render pass (see RenderContext / the game-frame Render contract): the renderer clears to the sky,
 * enters 3D mode with the camera, draws, and leaves 3D mode. It does not open or close the render target
 * pass itself, and it does not composite to the screen.
 *
 * THREADING. All calls are main-thread only (they drive the asset manager and Raylib rendering).
 */


// Forward declarations (referenced only by pointer; full types in their own headers).
/** @brief The asset manager; full type in AssetManager.h. Borrowed by the renderer. */
typedef struct AssetManagerStruct AssetManager;
/** @brief The logger; full type in Logger.h. Borrowed by the renderer; may be NULL. */
typedef struct LoggerStruct Logger;
/** @brief A live world; full type in World.h. */
typedef struct WorldStruct World;
/** @brief The game camera; full type in GameCamera.h. */
typedef struct GameCameraStruct GameCamera;
/** @brief A per-pass render context; full type in Renderer.h. */
typedef struct RenderContextStruct RenderContext;

/** @brief The world renderer. Opaque; construct with WorldRenderer_Create. */
typedef struct WorldRendererStruct WorldRenderer;


// Functions.
/**
 * @brief Creates a world renderer bound to an asset manager (mints its own asset user).
 * @param assetManager The asset manager used to resolve object assets; borrowed, must outlive the renderer. Must not be NULL.
 * @param logger The logger for asset-resolution diagnostics; borrowed, may be NULL.
 * @param outRenderer [out] Receives the new renderer on success, NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p assetManager or @p outRenderer is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldRenderer_Create(AssetManager* assetManager, Logger* logger, WorldRenderer** outRenderer);

/**
 * @brief Releases the renderer, dropping its holds on every asset it resolved and retiring its asset user.
 * @param self The renderer, or NULL.
 * @returns Success, or the first non-success Error encountered while releasing assets.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldRenderer_Deconstruct(WorldRenderer* self);

/**
 * @brief Loads the assets referenced by the world's objects, so rendering does not stall on first use.
 *
 * Resolves each renderable object's asset once through the asset manager (attributed to the renderer's
 * user). Assets that fail to load are logged and skipped, not treated as fatal, so a missing asset does
 * not stop the world from rendering. Safe to call again after the world's objects change.
 * @param self The renderer; must not be NULL.
 * @param world The world whose assets to load; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p world is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldRenderer_PrepareWorld(WorldRenderer* self, World* world);

/**
 * @brief Renders the world from the camera into the active render pass.
 *
 * Must be called INSIDE an active pass opened by RenderContext_BeginRendering (e.g. from a game frame's
 * Render). Clears the target to the sky color, enters 3D mode with the camera, draws the debug grid (if
 * enabled) and every model object, then leaves 3D mode. Missing/failed assets are skipped silently (they
 * are reported by WorldRenderer_PrepareWorld).
 * @param self The renderer; must not be NULL.
 * @param world The world to draw; must not be NULL.
 * @param camera The camera to view from; must not be NULL.
 * @param context The active render context; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if any argument is NULL.
 */
Error WorldRenderer_Render(WorldRenderer* self, World* world, const GameCamera* camera, RenderContext* context);

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
