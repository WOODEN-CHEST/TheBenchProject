#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// RenderTexture2D (the frame target the renderer blits its result into) is a Raylib type in this API,
// so the Raylib include is unavoidable here (as in the other rendering headers).
#include "raylib/raylib.h"
#include "wr/WRError.h"


/**
 * @file WorldRenderer.h
 * @brief Renders a World through a GameCamera, owning the render-target pipeline. Separate from world data.
 *
 * The world model carries no rendering logic; the WorldRenderer turns a World + a GameCamera into pixels and
 * never writes to the world or to shared assets. It draws the world's model, sprite and light components, skipping
 * objects that are disabled or hidden. Model components reference models by name; the renderer resolves each
 * component's model once (again only when its name changes) and holds the models under its own asset user. Sprite
 * animations belong to the world (World_Construct2), so the renderer only reads each sprite's current frame.
 *
 * PIPELINE. The world is drawn into an internal floating-point (HDR) "scene" target, then tonemapped into the
 * caller's frame target. With pixelation enabled (default) the scene target is sized to the low pixel
 * resolution (~640 on the long axis, square pixels) and point-upscaled, giving the game's chunky pixel look.
 * In order: the shadow cascades of the sky's shadow light (the sun by day, the moon on bright nights) and the
 * point-light cube shadow; a normal/flag G-buffer and screen-space ambient occlusion (applied to the ambient term
 * only); the scene pass (atmospheric sky with the moon, the moonlit sky, the Milky Way and light pollution's skyglow,
 * the star field, PBR-lit models with the sun, the moon and forward-shaded point lights and distance fog, lit sprites
 * (cutout, then decals, then blended and additive ones back to front), debug grid and light gizmos; the sky's dim light
 * and the reflected light go through the eye's night vision, which greys them at night as the eye does);
 * depth/normal-edge outlines; bloom and sun shafts (added in HDR); the tonemap with eye adaptation; and last
 * the crisp overlay, which draws OmitPixelation objects at window resolution over the pixelated image.
 *
 * RENDER SPACE. Everything is drawn relative to the camera: the camera's position (a double) is each frame's origin,
 * every position the GPU sees is a position minus that origin, subtracted in double and rounded once, and the view only
 * turns. So nothing shakes far from the world's origin (the sky's rays come from the rotation alone), and the shadow
 * cascades, centred on the camera, snap to their texel grids so their edges stand still while the camera moves.
 *
 * VISIBILITY. Each frame the camera's passes draw only what is in the camera's view; each shadow cascade draws the
 * casters in its own box (a caster behind the camera still shadows what is in view), the cube shadow the casters in its
 * light's reach. Every object has its own draw distance (its world model's or world sprite's, a sprite's kind's when
 * its world sprite gives none: 150 m, decals 60 m, or its placement's own; times the config's draw_distance_scale unless
 * it is fixed, and pushed out by a narrower field of view): beyond it the object is not drawn, and over its last tenth
 * (at least 3 m) it fades out. An object's opacity (its tint's) fades it the same way. Models and cutout sprites show
 * a fade as a 4x4 ordered dither on the scene's pixel grid, in every pass, so a fading object's shadow thins with it;
 * the G-buffer draws it solid until half its fade and not after, so its outline switches off once. Blended and
 * additive sprites fade their alpha instead.
 *
 * LODS. Every model draws the LOD of its world model that its distance picks (the camera's distance to its centre,
 * pushed out by a narrower field of view, against its switch distances times the config's lod_distance_scale, with a
 * hysteresis; or a forced LOD), and a change cross-fades over the world model's fade time: both LODs are drawn with
 * complementary dithers, so every pixel shows one of them. The G-buffer draws the LOD with the larger share, the
 * shadows the LOD it heads to, both solid.
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
/** @brief A collision world whose shapes the renderer can draw for debugging; full type in WorldCollision.h. */
typedef struct WorldCollisionStruct WorldCollision;

/** @brief The world renderer. Opaque; construct with WorldRenderer_Create. */
typedef struct WorldRendererStruct WorldRenderer;


// Macros.
/** @brief How many LODs the status counts drawn models for (WorldRendererStatus.DrawnModelsByLod): a world model's most,
 *  WORLD_MODEL_MAX_LODS (WorldModel.h). */
#define WORLD_RENDERER_STATUS_LOD_COUNT 8U


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
    /** @brief The sprites: the lit sprite shader and the sprite quad. Without them sprites are not drawn at all. */
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
    /** @brief Sprite lighting: lit sprites take the sun, the moon, lamps, shadows and the night vision like models
     *  (inactive while switched off with WorldRenderer_SetSpriteLightingEnabled: every sprite then draws unlit). */
    WorldRendererFeature_SpriteLighting,
    /** @brief Shadows cast by cutout sprites, facing the light (inactive while no sprite casts or no shadow runs). */
    WorldRendererFeature_SpriteShadows,
    /** @brief Skinned models bending with their animators' poses (T29): available with the skinned twin of the shader
     *  models draw with (world_pbr_skinned, or world_unlit_skinned while PBR lighting is unavailable) and its bone
     *  palette, active while a shown model has a pose. Without it skinned meshes draw in their bind pose (also in a
     *  pass whose own skinned twin is missing: the G-buffer's, a shadow's). */
    WorldRendererFeature_Skinning,
    /** @brief The lit terrain shader (world_terrain, B01): terrain chunks lit like models. Without it they draw unlit with
     *  raylib's default shader. */
    WorldRendererFeature_TerrainShading,
    /** @brief The terrains' materials from texture arrays (B02): active while a drawn terrain has its arrays; unavailable
     *  without texture arrays (a GL function missing, logged once: the terrains then show their materials' mean colours)
     *  or without the lit terrain shader. */
    WorldRendererFeature_TerrainMaterials,
    /** @brief Instanced drawing of model instances components (B03): every copy of a cell in one draw per mesh and LOD,
     *  each copy fading and changing LOD on its own. Unavailable without PBR lighting or its instanced twin
     *  (world_pbr_instanced); inactive while switched off (WorldRenderer_SetInstancingEnabled) or no cell is in view.
     *  Without it, or without a pass's own twin, the copies draw one at a time, looking the same. */
    WorldRendererFeature_Instancing,
    /** @brief The atmosphere's tables (T15): the transmittance, the multiple scattering and the sky view, built first in
     *  every frame (the first two only when the air changes); the sky and the fog read them, with the light the air
     *  scatters more than once. Unavailable without their shaders or targets; inactive while switched off
     *  (WorldRenderer_SetAtmosphereTablesEnabled). Without them the sky and the fog are marched per pixel, single
     *  scattering only. */
    WorldRendererFeature_AtmosphereTables,
    /** @brief The number of features; not a feature. */
    WorldRendererFeature_Count
} WorldRendererFeature;

/**
 * @brief What the terrains show (B02, a debug aid: WorldRenderer_SetTerrainView). Every view but the first colours each
 *        palette material with a debug colour of its own (green, brown, grey, sand, ... by palette index), lit.
 */
typedef enum WorldRendererTerrainViewEnum
{
    /** @brief The materials, blended and shaded (the default). */
    WorldRendererTerrainView_Shaded = 0,
    /** @brief The painted weights as they were painted (B01's colours): before the rules. */
    WorldRendererTerrainView_PaintedWeights = 1,
    /** @brief The weights after the terrain's rules (slope, height, noise). */
    WorldRendererTerrainView_RuleWeights = 2,
    /** @brief The material with the largest share after height blending, each pixel one flat colour. */
    WorldRendererTerrainView_DominantMaterial = 3,
    /** @brief The number of views; not a view. */
    WorldRendererTerrainView_Count = 4
} WorldRendererTerrainView;

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
    /** @brief Model components with a usable model (drawn unless their object is disabled or hidden). */
    size_t ModelCount;
    /** @brief Model components that draw nothing: no model name, or a model that failed to load (logged once). */
    size_t ModelWithoutAssetCount;
    /** @brief Of the shown models, those with a pose (T29): their skinned meshes bend with their animators. */
    size_t PosedModelCount;
    /** @brief Sprite components with a frame to draw, on enabled and visible objects. */
    size_t SpriteCount;
    /** @brief Of those, the decals. */
    size_t DecalCount;
    /** @brief Of those, the blended and additive sprites that are not decals (drawn sorted, back to front). */
    size_t TransparentSpriteCount;
    /** @brief Point lights on enabled and visible objects. */
    size_t LightCount;
    /** @brief Stars in the star catalogue (from the environment's StarDensity), whether or not they show now. */
    size_t StarCount;
    /** @brief Whether the camera's frustum and the cascades' boxes culled (WorldRenderer_SetCullingEnabled). */
    bool IsCullingEnabled;
    /** @brief Models and sprites the camera's passes drew. */
    size_t DrawnModelCount;
    size_t DrawnSpriteCount;
    /** @brief Models and sprites not drawn because they lay outside the camera's view (within their draw distance and
     *  not fully transparent). */
    size_t OutsideViewCount;
    /** @brief Models and sprites not drawn because they lay beyond their draw distance or were fully transparent. */
    size_t FadedOutCount;
    /** @brief Of the drawn models and sprites, those fading or translucent (dithered; blended and additive sprites
     *  alpha-faded). */
    size_t FadingCount;
    /** @brief Of the drawn models, how many showed each LOD ([i] counts LOD i: the LOD shown, or headed to). */
    size_t DrawnModelsByLod[WORLD_RENDERER_STATUS_LOD_COUNT];
    /** @brief Of the drawn models, those cross-fading between two LODs. */
    size_t LodFadingCount;
    /** @brief Whether the LOD tinting was on (WorldRenderer_SetLodTintingEnabled). */
    bool IsLodTintingEnabled;
    /** @brief Terrain components with a usable terrain, and those that draw none (no terrain named, or one that failed
     *  to load: logged once) (B01). */
    size_t TerrainCount;
    size_t TerrainWithoutAssetCount;
    /** @brief The chunks of the terrains on enabled and visible objects, those the camera's passes drew, and the
     *  triangles of the drawn ones. */
    size_t TerrainChunkCount;
    size_t DrawnTerrainChunkCount;
    size_t DrawnTerrainTriangleCount;
    /** @brief Whether the chunk key was on (WorldRenderer_SetTerrainKeyEnabled). */
    bool IsTerrainKeyEnabled;
    /** @brief What the terrains showed (WorldRenderer_SetTerrainView, B02). */
    WorldRendererTerrainView TerrainView;
    /** @brief Model instances components with a usable world model, and those that draw no copies (no world model named,
     *  or one that failed to load: logged once) (B03). */
    size_t InstanceComponentCount;
    size_t InstanceComponentWithoutAssetCount;
    /** @brief The copies of the components on enabled and visible objects, and their cells. */
    size_t InstanceCount;
    size_t InstanceCellCount;
    /** @brief The cells the camera's passes drew and their copies (each copy then fades and picks its LOD on its own),
     *  the cells outside the camera's view, and those beyond their draw distance or fully transparent. */
    size_t DrawnInstanceCellCount;
    size_t DrawnInstanceCount;
    size_t InstanceCellsOutsideViewCount;
    size_t InstanceCellsFadedOutCount;
    /** @brief Whether the copies drew instanced (WorldRenderer_SetInstancingEnabled). */
    bool IsInstancingEnabled;
    /** @brief Whether the atmosphere's tables were switched on (WorldRenderer_SetAtmosphereTablesEnabled), how many times
     *  their transmittance and multiple scattering tables were built (whenever the air changes), and the height above
     *  sea level the sky was seen from, in metres (the camera's, at least 1 m) (T15). */
    bool AreAtmosphereTablesEnabled;
    uint32_t AtmosphereTableBuildCount;
    float AtmosphereViewHeight;
    /** @brief How long the render took on the CPU, in milliseconds (WorldRenderer_RenderToTarget from its start to its
     *  return; the GPU works on after it, and its share shows in the frame time when it is the slower), and the time
     *  since the render before, as the caller gave it (its deltaSeconds; 0 when that was not above 0). */
    double RenderMilliseconds;
    double FrameMilliseconds;
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
 * Resolves every model component's model under a fresh asset user and then retires the previous one, so models
 * that no world object uses any more are released (asset holds are not counted, so this swap is how unused
 * models get released). Failures are logged once and skipped, not fatal. Components added later are resolved by
 * WorldRenderer_RenderToTarget automatically, but a model that failed is not retried until its component names
 * another: call this again after asset definitions change (new search roots, AssetManager_ReadDefinitions,
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
 * Brings the per-object cache up to date for the camera's position (new objects and renamed models are resolved here,
 * failures logged once; every position relative to the camera), draws the 3D world into the internal scene target (recreating the targets if the frame size or the
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
 * @brief Reports what the most recent render did: the scene target tier, each feature's state, object counts (what was
 *        drawn, culled and fading).
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
 * @brief Switches the atmosphere's tables on or off (default on), for A/B-ing the sky (T15).
 *
 * On, the sky and the fog's sky come from the tables built first in every frame, with the light the air scatters more
 * than once; off, they are marched per pixel as before the tables (single scattering only), with the same air.
 * Applied on the next render.
 * TODO: remove the switch and the direct march once the owner approves the tables' sky (T15).
 * @param self The renderer; must not be NULL.
 * @param enabled true to draw the sky from the tables.
 */
void WorldRenderer_SetAtmosphereTablesEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the atmosphere's tables are switched on (see WorldRenderer_SetAtmosphereTablesEnabled).
 * @param self The renderer; must not be NULL.
 * @returns true if they are on (they draw the sky when available).
 */
bool WorldRenderer_AreAtmosphereTablesEnabled(const WorldRenderer* self);

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
 * @brief Enables or disables sprite lighting: lit sprites taking the sun, the moon, lamps, shadows, wrap lighting and
 *        the night vision like the models around them.
 *
 * A code-side toggle for A/B-ing it: when disabled every sprite draws unlit at its full colours (as sprites did before
 * they were lit; unlit sprites keep their emission), still fogged, still casting shadows. Default enabled. Applied on
 * the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to light the lit sprites, false to draw every sprite unlit.
 */
void WorldRenderer_SetSpriteLightingEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether sprite lighting is enabled (see WorldRenderer_SetSpriteLightingEnabled).
 * @param self The renderer; must not be NULL.
 * @returns true if enabled.
 */
bool WorldRenderer_IsSpriteLightingEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables frustum culling: the camera's passes drawing only what is in its view, and each shadow
 *        cascade only the casters in its box.
 *
 * A code-side toggle for A/B-ing it: when disabled every object within its draw distance is drawn (the GPU clips what
 * lies outside the view), as before culling existed; the image must not change, only the frame time. Draw distances,
 * fades, opacity and the cube shadow's reach test stay. Default enabled. Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to cull, false to draw everything within its draw distance.
 */
void WorldRenderer_SetCullingEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether frustum culling is enabled (see WorldRenderer_SetCullingEnabled).
 * @param self The renderer; must not be NULL.
 * @returns true if enabled.
 */
bool WorldRenderer_IsCullingEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the LOD tinting: every model drawn with its LOD's colour multiplied into its albedo, so the
 *        LOD switches can be watched.
 *
 * A debug aid: LOD 0 keeps its colours (white), LOD 1 is green, 2 yellow, 3 red, coarser ones magenta, and LODs past 0
 * also glow faintly in their colour (night vision greys reflected colour, not emitted light, so the colours show at
 * night too); during a cross-fade the two LODs' dithers show both colours. Default disabled. Applied on the next
 * render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to tint the models by their LODs.
 */
void WorldRenderer_SetLodTintingEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the LOD tinting is enabled (see WorldRenderer_SetLodTintingEnabled).
 * @param self The renderer; must not be NULL.
 * @returns true if enabled.
 */
bool WorldRenderer_IsLodTintingEnabled(const WorldRenderer* self);

/**
 * @brief Enables or disables the terrain's chunk key (B01): every terrain chunk tinted by its cell (neighbouring chunks
 *        always differ) and the terrain's wireframe drawn over it, to see how a terrain is cut and how dense it is.
 *
 * A debug aid. Default disabled. Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to show the chunk key.
 */
void WorldRenderer_SetTerrainKeyEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether the terrain's chunk key is enabled (see WorldRenderer_SetTerrainKeyEnabled).
 * @param self The renderer; must not be NULL.
 * @returns true if enabled.
 */
bool WorldRenderer_IsTerrainKeyEnabled(const WorldRenderer* self);

/**
 * @brief Switches instanced drawing (B03): on, every pass draws all the copies of a cell of a model instances component
 *        at once (one draw per mesh and LOD); off, it draws each copy on its own through the ordinary shaders, with the
 *        same fades and LODs (an A/B for the instanced path: the picture must not change).
 *
 * A debug aid. Default enabled. Applied on the next render.
 * @param self The renderer; must not be NULL.
 * @param enabled true to draw instanced.
 */
void WorldRenderer_SetInstancingEnabled(WorldRenderer* self, bool enabled);

/**
 * @brief Reports whether instanced drawing is enabled (see WorldRenderer_SetInstancingEnabled).
 * @param self The renderer; must not be NULL.
 * @returns true if enabled.
 */
bool WorldRenderer_IsInstancingEnabled(const WorldRenderer* self);

/**
 * @brief Chooses what the terrains show (B02): their shaded materials, or a debug view of the blend's steps (the painted
 *        weights, the weights after the rules, the dominant material).
 *
 * A debug aid. Default WorldRendererTerrainView_Shaded. Applied on the next render. A value outside the enum is ignored.
 * @param self The renderer; must not be NULL.
 * @param view The view.
 */
void WorldRenderer_SetTerrainView(WorldRenderer* self, WorldRendererTerrainView view);

/**
 * @brief Reports what the terrains show (see WorldRenderer_SetTerrainView).
 * @param self The renderer; must not be NULL.
 * @returns The view.
 */
WorldRendererTerrainView WorldRenderer_GetTerrainView(const WorldRenderer* self);

/**
 * @brief The name of a terrain view, for log lines ("shaded", "painted weights", "rule weights", "dominant material").
 * @param view The view.
 * @returns A static string; "unknown" for a value outside the enum.
 */
const unsigned char* WorldRenderer_GetTerrainViewName(WorldRendererTerrainView view);

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
 * @brief Sets the collision world whose shapes are drawn as wireframes (a debug aid), or NULL to draw none (the
 *        default).
 *
 * The renderer only reads the collision world's debug lists (WorldCollision_GetDebugShapes and, since T27,
 * WorldCollision_GetDebugContacts; rule 3), so nothing shows until WorldCollision_SetDebugListEnabled or
 * WorldCollision_SetDebugContactsEnabled turns a list on. The shapes are drawn in the scene pass after the light gizmos,
 * depth-tested, coloured by layer (triggers violet whatever their layer; static shapes darker); the contact points as
 * small crosses (white touching, grey near) with their normals (cyan) and pushes (red, along the normal).
 * @param self The renderer; must not be NULL.
 * @param collision The collision world (borrowed: set NULL before destroying it), or NULL.
 */
void WorldRenderer_SetCollisionDebug(WorldRenderer* self, const WorldCollision* collision);

/**
 * @brief Returns the collision world whose shapes are drawn, or NULL.
 * @param self The renderer; must not be NULL.
 * @returns The collision world set by WorldRenderer_SetCollisionDebug (borrowed), or NULL.
 */
const WorldCollision* WorldRenderer_GetCollisionDebug(const WorldRenderer* self);

/**
 * @brief Names the object the camera rides (T30: the player's body in first person), or none (0).
 *
 * From the next frame its world model's first_person block takes effect in the camera's passes (the G-buffer, the lit
 * pass, the unlit fallback): the bones it hides, and their subtrees, are drawn collapsed to their joint, so the camera
 * at the eyes does not see the inside of the head. The shadow passes draw the whole body. An object without a pose, or
 * whose world model has no first_person block, draws as any other.
 * @param self The renderer; must not be NULL.
 * @param objectId The object's id, or 0.
 */
void WorldRenderer_SetFirstPersonObject(WorldRenderer* self, uint64_t objectId);

/**
 * @brief Returns the object named by WorldRenderer_SetFirstPersonObject, or 0.
 * @param self The renderer; must not be NULL.
 * @returns The object's id, or 0.
 */
uint64_t WorldRenderer_GetFirstPersonObject(const WorldRenderer* self);

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
