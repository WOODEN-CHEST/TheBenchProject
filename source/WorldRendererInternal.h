#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WorldRenderer.h"
#include "WorldRenderCache.h"
#include "AssetManager.h"
#include "GameShader.h"
#include "GameModel.h"
#include "GameTexture.h"
#include "GameCamera.h"
#include "World.h"
#include "WorldAnimatorComponent.h"
#include "WorldEnvironment.h"
#include "WorldModelComponent.h"
#include "WorldInstancing.h"
#include "WorldSpriteComponent.h"
#include "GLTextureArray.h"
#include "GLReadback.h"
#include "GLDepth.h"
#include "EyeAdaptation.h"
#include "NightVision.h"
#include "WorldAtmosphere.h"
#include "WorldSkyLight.h"
#include "SpriteTransform.h"
#include "WorldRenderSpace.h"
#include "WorldVisibility.h"
#include "raylib/raylib.h"
// rlgl supplies RL_MAX_SHADER_LOCATIONS and the scene near/far cull distances.
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): the WorldRenderer's state and the functions its implementation files share.
 *
 * The renderer is split by render pass, one file per pass:
 *   WorldRenderer.c          the public API; builds the per-frame context and runs the passes in order
 *   WorldRendererCommon.c    shared helpers (config multipliers, colours, shader loading, target constructors)
 *   WorldRendererScene.c     the scene target (size, HDR tiers, recreation on resize) and the scene pass
 *   WorldRendererGeometry.c  model meshes (with per-draw materials) and light gizmos
 *   WorldRendererSprites.c   the sprites: the quad, the lit sprite shader, each frame's sprite matrices, and the lit
 *                            cutout, decal and sorted transparent draws
 *   WorldRendererLighting.c  the PBR shader, the lit shaders' frame uniforms, forward point lights, eye adaptation
 *   WorldRendererShadows.c   the shadow cascades of the sun or the moon, and the point-light cube shadow
 *   WorldRendererGBuffer.c   the normal/flag G-buffer and the ambient-occlusion pre-pass
 *   WorldRendererAtmosphere.c the atmosphere's tables: transmittance, multiple scattering, the sky view, built
 *                            first in the frame and read by the sky and the stars; the haze's volume; the sky's
 *                            irradiance table and the sky's light by height in the volume's last row
 *   WorldRendererSky.c       the atmospheric sky with the moon, the Milky Way and light pollution's skyglow
 *   WorldRendererStars.c     the star field (the seeded catalogue as one-pixel quads)
 *   WorldRendererPost.c      outlines, bloom, sun shafts and the tonemap
 *   WorldRendererExposure.c  the eye's adaptation to the picture: the meter pass, its readback (GLReadback.h,
 *                            direct GL calls) and the exposure (EyeAdaptation.h)
 * The camera's passes (the scene, the G-buffer, the crisp overlay) draw with reversed depth where the GPU has clip
 * control (GLDepth.h, direct GL calls; WorldRendererCommon_BeginCameraDepth and the rest): a 32-bit float depth
 * from 1 at the near plane to 0 at infinity and no far plane, so whatever has a long draw distance draws at any distance.
 *   WorldRendererCrisp.c     the crisp overlay for OmitPixelation objects
 *   WorldRendererTerrain.c   the terrain: its chunks' GPU meshes, the lit terrain shader, its draws in every pass,
 *                            the chunk key, and its materials' texture arrays (GLTextureArray.h, direct GL calls) and
 *                            their uniforms
 *   WorldRendererInstances.c the model instances components' copies: their records' GPU buffers, the instanced
 *                            twins of the geometry passes, their instanced draws in every pass and the one-at-a-time
 *                            path (the A/B and every fallback)
 * plus the per-object render cache (WorldRenderCache.h) and each frame's visibility (WorldVisibility.h: what each pass
 * draws, with every entry's fade). Each pass keeps its shader, uniform locations, targets and toggles in its own state
 * struct below.
 *
 * RENDER SPACE (WorldRenderSpace.h). Every pass draws in render space: the world's orientation with the camera at
 * (0, 0, 0). The frame's Origin is the camera's world position in double; the render cache, synced for it, holds every
 * position relative to it (subtracted in double, rounded once), and the passes, the uniforms and the debug draws use
 * only those render-space values and the frame's render-space matrices. Never round a world position to float and
 * subtract the origin after: convert through GameMath's relative functions (GameMath_ToVector3Relative,
 * GameMath_ToMatrixRelative).
 *
 * RULES. Render reads the world and shared assets and never writes them: meshes draw with materials built on
 * the stack, and the PBR shader's location table is a copy owned here. Renderer-owned samplers sit on the fixed
 * units of WorldRendererTextureUnit, pointed there once at load. A missing shader or target disables its
 * feature with one log line (WorldRendererCommon_WarnOnce, or the load failure the asset manager logs).
 */


// Macros.
/** Bookkeeping value Raylib uses for a RenderTexture's depth attachment format (24-bit depth renderbuffer). */
#define SCENE_DEPTH_FORMAT 19
/** sRGB gamma used to convert stored 8-bit colours to/from linear light. */
#define SRGB_GAMMA 2.2f
/** Near/far planes the scene 3D pass uses (Raylib's global cull distances, which BeginMode3D applies; the far plane
 *  only without reversed depth); the frame context rebuilds the same projection for the passes that reconstruct
 *  view positions from depth. */
#define SCENE_NEAR_PLANE ((double)RL_CULL_DISTANCE_NEAR)
#define SCENE_FAR_PLANE ((double)RL_CULL_DISTANCE_FAR)
/** Resolution (square) of each sun shadow-map cascade, in texels. The PBR shader's PCF needs its texel size. */
#define SHADOW_MAP_SIZE 2048
/** Number of maps in a raylib Material: the renderer module's RENDERER_MATERIAL_MAP_COUNT (raylib's private
 *  MAX_MATERIAL_MAPS, see BUILDING.md). DrawMesh reads, and binds textures from, exactly this many entries of
 *  Material.maps, so a per-draw material copy must have this many. Changing it means checking
 *  WorldRendererTextureUnit. */
#define WORLD_RENDERER_MATERIAL_MAP_COUNT RENDERER_MATERIAL_MAP_COUNT


// Types.
/* Fixed texture units for the PBR shader's renderer-owned samplers. They sit above raylib's material map units
 * (DrawMesh binds and unbinds units 0..WORLD_RENDERER_MATERIAL_MAP_COUNT-1) and the 2D batch's units (0..4), so
 * neither can overwrite them. WorldRendererLighting_LoadShader points each sampler at its unit ONCE; per-frame
 * code only binds textures. That also keeps every sampler off unit 0 even when its feature is inactive:
 * OpenGL forbids samplers of different types (here the samplerCube and the albedo sampler2D) on one unit,
 * whether or not the shader samples them. */
typedef enum
{
    // The atmosphere's tables, for the programs that read them (the sky pass, the stars): raylib binds only cube
    // maps on 7 and 8 (MATERIAL_MAP_CUBEMAP and _IRRADIANCE, which no material of ours fills), never a 2D texture, and
    // the 2D batch only units 0 to 4, so what a pass binds there stays bound until another pass binds over it: the
    // atmosphere pass leaves the tables bound and the sky pass and the stars bind them again before their draws; the lit
    // passes bind the volume on 8 (below), so a lit program never reads the sky view there.
    WorldRendererTextureUnit_AtmosphereTransmittance = 7, // sampler2D atmosphereTransmittanceTable
    WorldRendererTextureUnit_AtmosphereSkyView = 8,       // sampler2D atmosphereSkyViewTable
    // The aerial-perspective volume in the lit programs, on the unit the sky view has in the sky pass and the
    // stars: the lit programs never read the sky view (the 2D passes that read the volume bind it with
    // SetShaderValueTexture).
    WorldRendererTextureUnit_HazeVolume = 8,              // sampler2D atmosphereVolume
    // The terrain program's material arrays: sampler2DArray, below raylib's material map units' top, because the
    // terrain program draws with no material maps (DrawMesh binds none for it). Other programs' material maps bind
    // GL_TEXTURE_2D (or cube) textures on these units; an array sampler reads only the units' GL_TEXTURE_2D_ARRAY
    // target, which nothing else uses. The units' budget, and what features not built yet will need, is in
    // agents/WorldRuntime.md ("Renderer structure").
    WorldRendererTextureUnit_TerrainAlbedoHeight = 9,   // sampler2DArray materialAlbedoHeight
    WorldRendererTextureUnit_TerrainNormal = 10,        // sampler2DArray materialNormal
    WorldRendererTextureUnit_TerrainRoughnessAo = 11,   // sampler2DArray materialRoughnessAo
    // Every lit program's frame textures.
    WorldRendererTextureUnit_ShadowNear = 12,       // sampler2D shadowMap (near sun cascade)
    WorldRendererTextureUnit_ShadowFar = 13,        // sampler2D shadowMapFar (far sun cascade)
    WorldRendererTextureUnit_PointShadowCube = 14,  // samplerCube pointShadowCube
    WorldRendererTextureUnit_AmbientOcclusion = 15, // sampler2D ambientOcclusionMap
} WorldRendererTextureUnit;

_Static_assert(WorldRendererTextureUnit_ShadowNear >= WORLD_RENDERER_MATERIAL_MAP_COUNT,
    "renderer-owned texture units must sit above raylib's material map units");
_Static_assert(WorldRendererTextureUnit_TerrainRoughnessAo < WorldRendererTextureUnit_ShadowNear,
    "the terrain's arrays sit below the frame textures");
_Static_assert(WorldRendererTextureUnit_HazeVolume < WorldRendererTextureUnit_TerrainAlbedoHeight,
    "the haze's volume sits below the terrain's arrays (the terrain program reads both)");
_Static_assert(WORLD_RENDERER_HAZE_SLICE_COUNT == WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT,
    "the public haze slice count is the volume's");

/* Which objects a draw pass should include, by their OmitPixelation flag. The pixelated scene pass draws only
 * pixelated objects (when the crisp overlay is active), the crisp overlay pass draws only the flagged ones, and
 * the shadow passes draw all (everything casts shadows). */
typedef enum
{
    WorldRendererPixelFilter_All,
    WorldRendererPixelFilter_PixelatedOnly, // OmitPixelation == false
    WorldRendererPixelFilter_CrispOnly,     // OmitPixelation == true
} WorldRendererPixelFilter;

/** @brief One light's linear radiance (colour x intensity), parallel to the forward-light culling list. */
typedef struct WorldRendererRadianceStruct
{
    /** @brief Linear RGB radiance. */
    float Rgb[3];
} WorldRendererRadiance;

/** @brief One shadow cascade's light matrices and box for a frame, in render space (WorldRendererShadows_PrepareFrame:
 *  WorldRenderSpace_GetCascade). */
typedef struct WorldRendererCascadeFrameStruct
{
    /** @brief The light's view (looking along the shadow light at the box's centre: the camera's position snapped to the
     *  cascade's texel grid) and its orthographic projection. */
    Matrix View;
    Matrix Projection;
    /** @brief Render space -> light clip (raymath order: MatrixMultiply(View, Projection)): the lit shaders' lightVP or
     *  lightVPFar. */
    Matrix ViewProjection;
    /** @brief The cascade's box as six planes (from ViewProjection): its casters are culled against it. */
    WorldFrustum Box;
} WorldRendererCascadeFrame;

/** @brief The moon's disc in the sky for one frame (WorldRendererSky_PrepareFrame), as the sky shader draws it. */
typedef struct WorldRendererMoonDiscStruct
{
    /** @brief The disc's axes on the sky, both perpendicular to the moon's direction: Right to the right of the face as
     *  seen from the ground with its north toward the sky's pole, Up toward that pole. The face therefore turns as the
     *  moon crosses the sky, like the real one. */
    Vector3 Right;
    Vector3 Up;
    /** @brief Cosine and sine of the disc's angular radius. */
    float CosRadius;
    float SinRadius;
    /** @brief Linear radiance of a white texel lit head-on (MoonBrightness included); the shader shades the phase. */
    float Radiance;
    /** @brief The dark side's glow (earthshine) as a share of Radiance, already x (1 - illumination). */
    float Earthshine;
    /** @brief How opaque the disc is, 0..1: it fades out within about 12 degrees of the sun (where the real moon is
     *  lost in the glare), which also keeps the virtual moon from eclipsing the sun every new moon. */
    float Opacity;
} WorldRendererMoonDisc;

/**
 * @brief Everything one render decides up front and shares between its passes.
 *
 * Built by WorldRenderer_RenderToTarget at the start of every render and passed to each pass, so the passes
 * do not recompute the camera matrices or re-decide what runs. Pointers are borrowed for the render only.
 */
typedef struct WorldRenderFrameStruct
{
    /** @brief The world being drawn (read only). */
    World* World;
    /** @brief The world's environment. */
    const WorldEnvironment* Environment;
    /** @brief The frame's origin (render space's (0, 0, 0)): the camera's world position, in double. The render cache
     *  was synced for it, so its render-space values are relative to it. */
    Vector3Double Origin;
    /** @brief raylib's camera for BeginMode3D: at (0, 0, 0), looking along the camera's forward
     *  (GameCamera_ToRenderCamera). */
    Camera3D RenderCamera;
    /** @brief The camera's zoom factor (WorldVisibility_GetZoomFactor of its field of view): the draw and LOD distances
     *  are measured against distances times it. */
    float ZoomFactor;
    /** @brief The scene's view (render space -> view space: a pure rotation) and projection (what BeginMode3D builds
     *  for the scene target), the projection's inverse, and the inverse of view x projection (clip -> render space: the
     *  sky's rays). */
    Matrix View;
    Matrix Projection;
    Matrix InvProjection;
    Matrix InvViewProjection;
    /** @brief View x projection (raymath order: MatrixMultiply(View, Projection)): render space -> clip. */
    Matrix ViewProjection;
    /** @brief The camera's frustum in render space (from ViewProjection): what the camera's passes (scene, G-buffer,
     *  crisp) cull against. */
    WorldFrustum CameraFrustum;
    /** @brief The sky's sun: unit direction to it (the sky, the haze, the sun shafts and the outlines' creases use it),
     *  and how much daylight there is (1 day .. 0 night). */
    Vector3 SunDirection;
    float Daylight;
    /** @brief The sky's moon: unit direction to it, its phase in [0, 1) and its lit fraction. */
    Vector3 MoonDirection;
    float MoonPhase;
    float MoonIllumination;
    /** @brief The stars' rotation for the date and the time of day (WorldEnvironment_GetStarsToWorld): turns the star
     *  catalogue and the Milky Way. */
    Matrix StarsToWorld;
    /** @brief Render time in seconds, accumulated by the renderer and wrapped every hour (float precision), for
     *  animated effects such as twinkling. Renderer-side state, not world time. */
    float TimeSeconds;
    /** @brief Night sky values: the faintest magnitude visible overhead (WorldRendererSky_PrepareNightSky: the eye's
     *  threshold against the twilight, the moonlit sky and light pollution), the Milky Way's linear radiance per white
     *  texel (its brightness by that limit), and light pollution's skyglow at the horizon (WorldRendererSky_PrepareFrame:
     *  linear; 0 by day), which the sky's direct march adds (the tables hold light pollution's light in the air instead:
     *  LightPollutionRadiance). */
    float LimitingMagnitude;
    float MilkyWayRadiance;
    float SkyglowRadiance[3];
    /** @brief The moon's light on the atmosphere, relative to the sun's above the air (the moonlit sky); 0 by day,
     *  without a moon, or while the moon is far below the horizon (WorldRendererSky_PrepareFrame: all of the moon's
     *  light above the air, its work-saving gates in). */
    float MoonSkyIntensity;
    /** @brief The moon's disc, valid while IsMoonActive. */
    WorldRendererMoonDisc MoonDisc;
    /** @brief The frame target (window) size and the scene target size, in pixels. */
    int WindowWidth;
    int WindowHeight;
    int SceneWidth;
    int SceneHeight;
    /** @brief Which objects the pixelated passes (G-buffer, scene) draw: PixelatedOnly while the crisp overlay
     *  runs, All otherwise. */
    WorldRendererPixelFilter SceneFilter;

    /** @brief The sky's lights above the air (WorldEnvironment_GetSkyLights: the sun's intensity, the moon's with its
     *  phase and the daylight gate in). */
    float SunIntensity;
    float MoonIntensity;
    /** @brief The sky's light at the camera (WorldRendererLighting_PrepareSkyLight: the C mirror's,
     *  WorldSkyLight_GetFrameLight). The lit shaders take it as their uniforms while IsSkyLightPerHeight is false. */
    WorldSkyLightFrame SkyLight;
    /** @brief Whether the lit shaders take the sky's light by height from the volume's last row (while the atmosphere's
     *  tables and the volume run); false: SkyLight's, the camera's, for every fragment. Whether the moon
     *  lights anything this frame (its terms are skipped otherwise). */
    bool IsSkyLightPerHeight;
    bool IsMoonLit;
    /** @brief The light the air is lit by: the sun and the moon above the air as multiples of WORLD_ATMOSPHERE_SUN_LIGHT,
     *  colour in (the atmosphere's shaders' intensity uniforms). The moon's is 0 while the moonlit sky does not show
     *  (MoonSkyIntensity 0). */
    float AirSunIntensity[3];
    float AirMoonIntensity[3];
    /** @brief The sun's disc (the sky shader's radiance is SUN_DISC_RADIANCE x this, colour in) and the moon's disc's
     *  linear colour. */
    float SunDiscIntensity[3];
    float MoonColor[3];
    /** @brief The exposure of the last render, which the eye is adapted to before this frame's is set (the sun's disc
     *  and the sun shafts' source limit are drawn against it). */
    float LastExposure;
    /** @brief The light the shadow cascades are rendered from this frame (the brighter one once it stands high
     *  enough), its unit direction, and how strong its shadows are (0..1: faded near the horizon and while the lights
     *  hand over). The cascades only run while ShadowLight is not None (IsCascadeShadowActive). */
    WorldShadowLight ShadowLight;
    Vector3 ShadowDirection;
    float ShadowFade;
    /** @brief The shadow cascades' light matrices and boxes (WorldRendererShadows_PrepareFrame), valid while
     *  IsCascadeShadowActive (the far one is drawn only while its map exists). */
    WorldRendererCascadeFrame NearCascade;
    WorldRendererCascadeFrame FarCascade;
    /** @brief The cube shadow's light: its position in render space (its RenderPosition), and the far range its packed
     *  distances are normalized by (its reach, at least twice the faces' near plane), which is also the reach its
     *  casters must touch; valid while IsPointShadowActive. */
    Vector3 PointShadowPosition;
    float PointShadowFar;
    /** @brief Lighting shared by the scene and crisp passes (computed once per render): the scene luminance estimate of
     *  the sky's light at the camera (WorldSkyLight_GetSceneLuminance), the field luminance night vision compares with
     *  before the lamps (NightVision_GetFieldLuminance of its daylight's and night lights' shares), and the shadow
     *  strength. */
    float BaseSceneLuminance;
    float NightVisionField;
    float ShadowStrength;

    /** @brief Whether each optional pass runs this render. */
    bool IsCascadeShadowActive;
    bool IsPointShadowActive;
    bool IsAoActive;
    bool AreOutlinesActive;
    bool IsBloomActive;
    bool AreSunshaftsActive;
    bool IsCrispActive;
    bool AreStarsActive;
    bool IsMilkyWayActive;
    /** @brief Whether the moon's disc is drawn (the sky pass draws it, the star pass hides the stars behind it). */
    bool IsMoonActive;
    /** @brief Index (into the render cache's lights) of the light casting the cube shadow; valid when
     *  IsPointShadowActive. */
    size_t ShadowLightIndex;
    /** @brief Effective strengths of the passes that have one (world x config x base; 0 when inactive). */
    float AoStrength;
    float BloomStrength;
    float SunshaftStrength;
    /** @brief Where the sun projects on screen ([0,1] UV), for the sun shafts; valid when AreSunshaftsActive. */
    Vector2 SunScreenUv;
    /** @brief The tonemap exposure from eye adaptation; set just before the tonemap. */
    float Exposure;
    /** @brief The eye's adaptation this frame (set with Exposure): the luminance it is adapted to (the view's
     *  log-average, game units; the anchor's before the first measurement, 0 before the eye started), the real
     *  luminance that stands for (cd/m^2, EyeAdaptation_GetRealScale), and whether a measurement of the picture set it. */
    float AdaptedLuminance;
    float AdaptedRealLuminance;
    bool IsExposureMetered;
    /** @brief Whether the camera's passes draw with reversed depth this frame (the renderer's _depth._isReversed): the
     *  projection is then the reversed infinite one, the depth targets 32-bit float cleared to 0, and every reader of
     *  their depth takes it so (shaders/common/depth.glsl's depthReversed). */
    bool IsDepthReversed;
    /** @brief The atmosphere this frame (WorldRendererAtmosphere_PrepareFrame): the air's coefficients
     *  (WorldAtmosphere_GetMedium of the environment), the camera's height above sea level the sky is seen from (the
     *  origin's Y, clamped: WorldAtmosphere_GetViewHeight), and whether the tables hold the sky and the haze (the
     *  pass is available and switched on); false: the direct march (the A/B, the fallback). */
    WorldAtmosphereMedium AtmosphereMedium;
    float AtmosphereViewHeight;
    bool AreAtmosphereTablesActive;
    /** @brief The haze this frame (WorldRendererAtmosphere_PrepareFrame): the environment's and the player's
     *  (WorldAtmosphere_GetHaze), light pollution's light in the air at the camera's foot (linear; 0 by day:
     *  WorldAtmosphere_GetLightPollutionRadiance), and whether the volume holds it (built this frame: the tables are
     *  active, the volume is available and the haze's distance scale is above 0); false: no haze on anything. */
    WorldAtmosphereHaze Haze;
    float LightPollutionRadiance[3];
    bool IsHazeActive;
    /** @brief The eye's night vision this frame, which the sky and PBR shaders apply: how strongly (the renderer toggle
     *  x the world's NightVisionStrength; 0 = off), the field luminance it compares with (NightVisionField: the sky's
     *  light, without nearby lamps), the rods' grey (linear, luminance 1, from the world's NightVisionBlueShift), and
     *  whether it changes anything (on, with the field below the full-colour luminance: dusk and night). */
    float NightVisionStrength;
    float NightVisionFieldLuminance;
    float NightVisionTint[3];
    bool IsNightVisionActive;
    /** @brief The night vision's calibration (NightVision_GetCalibration), the renderer's, for the shaders. */
    NightVisionCalibration NightVision;
} WorldRenderFrame;

/**
 * @brief The dither uniforms of a geometry shader (shaders/common/dither.glsl): the draw's fade, its part of a LOD
 *        cross-fade (the progress, whether it is the incoming LOD), and the pass's pixels per scene pixel. -1 for a
 *        uniform the shader lacks.
 *
 * Filled by WorldRendererCommon_PrepareDither, which also uploads the defaults (solid, no cross-fade, scale 1).
 */
typedef struct WorldRendererDitherLocationsStruct
{
    int _locFade;
    int _locLodProgress;
    int _locIsIncomingLod;
    int _locPixelScale;
} WorldRendererDitherLocations;

/**
 * @brief A skinned shader's bone palette (shaders/common/skinning.glsl): where it goes, how many bones it holds,
 *        and which pose it holds this frame, so the meshes of one object upload it once per pass.
 *
 * Filled by WorldRendererCommon_PreparePalette; WorldRendererCommon_UploadPalette fills it from a pose. Every frame
 * starts by forgetting the pose (WorldRendererGeometry_ResetPalettes): by then another animator's pose may live where
 * the last one did.
 */
typedef struct WorldRendererPaletteStruct
{
    /** @brief The palette's location (bonePalette), -1 when the shader has none. */
    int _location;
    /** @brief How many bones it holds (the array's length over 3, read from the program): an object whose skeleton has
     *  more draws its skinned meshes in their bind pose. */
    uint32_t _maxBones;
    /** @brief The pose it holds and that pose's revision; NULL from the start of each frame to the first upload. */
    const WorldAnimatorPose* _pose;
    uint32_t _revision;
} WorldRendererPalette;

/**
 * @brief The shaders one geometry pass draws an object's meshes with: its rigid meshes with Rigid, its skinned ones
 *        (GameSkeleton_IsMeshSkinned) with Skinned while the object has a pose (WorldRenderModel.Pose) that Palette can
 *        hold. A plain value type; everything in it is borrowed for the pass.
 */
typedef struct WorldRendererMeshShadersStruct
{
    /** @brief The shader rigid meshes draw with, or NULL: each material's own. */
    const Shader* Rigid;
    /** @brief The shader skinned meshes draw with, or NULL: they draw as rigid ones, in their bind pose. */
    const Shader* Skinned;
    /** @brief Skinned's palette; may be NULL while Skinned is. */
    WorldRendererPalette* Palette;
    /** @brief Whether the pass draws the camera's view (the entries' CameraPose: the first-person object without its
     *  hidden bones) or a light's (their full Pose). */
    bool IsCameraView;
} WorldRendererMeshShaders;

/** @brief The scene render target the 3D world is drawn into (pixel resolution when pixelating). */
typedef struct WorldRendererSceneStateStruct
{
    /** @brief Internal scene target the 3D world renders into (pixel resolution when pixelating). */
    RenderTexture2D _target;
    /** @brief Whether _target has been created. */
    bool _hasTarget;
    /** @brief Whether _target's depth attachment is a samplable texture (required by the post passes). */
    bool _isDepthSamplable;
    /** @brief Whether the scene target is a floating-point (HDR) framebuffer; false = 8-bit fallback. */
    bool _isHdr;
    /** @brief Set once the HDR/fallback status has been logged, so it is reported only once. */
    bool _isTierReported;
    /** @brief Current width and height of _target, in pixels. */
    int _width;
    int _height;
    /** @brief The window (frame target) size the resolution-dependent targets were made for. */
    int _windowWidth;
    int _windowHeight;
} WorldRendererSceneState;

/**
 * @brief The uniform locations of a shader that includes shaders/common/atmosphere.glsl: the frame's air, the height
 *        the sky is seen from, whether the tables hold the sky, and the tables' samplers. -1 for a uniform the shader
 *        lacks (or the compiler dropped).
 *
 * Filled by WorldRendererAtmosphere_PrepareShader, which also points the transmittance and sky-view samplers at their
 * fixed units; WorldRendererAtmosphere_UploadFrame uploads a frame's values.
 */
typedef struct WorldRendererAtmosphereLocationsStruct
{
    int _locRayleighScattering;
    int _locMieScattering;
    int _locMieExtinction;
    int _locMieHeight;
    int _locMieAnisotropy;
    int _locOzoneAbsorption;
    int _locViewHeight;
    int _locTablesActive;
    int _locTransmittanceTable;
    int _locMultiscatterTable;
    int _locSkyViewTable;
    int _locIrradianceTable;
} WorldRendererAtmosphereLocations;

/**
 * @brief The uniform locations of a shader that includes shaders/common/haze.glsl: whether the volume holds this
 *        frame's haze, its sampler, the frame's haze (the distance scale, the start) and the ground haze. -1 for a
 *        uniform the shader lacks (or the compiler dropped).
 *
 * Filled by WorldRendererAtmosphere_PrepareHazeShader; WorldRendererAtmosphere_UploadHaze uploads a frame's values.
 */
typedef struct WorldRendererHazeLocationsStruct
{
    int _locActive;
    int _locVolume;
    int _locDistanceScale;
    int _locStartDistance;
    int _locGroundHazeDensity;
    int _locGroundHazeThickness;
    int _locGroundHazeHeight;
    int _locGroundHazeAlbedo;
    int _locGroundHazeGlow;
} WorldRendererHazeLocations;

/**
 * @brief The uniform locations of a shader that includes shaders/common/lighting.glsl (the PBR and the sprite
 *        shaders): the frame's lights, shadows, AO, haze and night vision, and the per-object forward point lights and
 *        haze multiplier.
 *
 * Filled by WorldRendererLighting_PrepareLitShader; -1 for a uniform the shader lacks (or optimized out).
 */
typedef struct WorldRendererLitLocationsStruct
{
    /** @brief How many point lights the shader's uniform arrays hold, capped at WORLD_MAX_FORWARD_LIGHTS
     *  (checked at load; uploads never exceed it). */
    int _maxPointLights;
    /** @brief The two directional lights' directions (they light the ground haze too) and the sky's light: whether it
     *  comes by height from the volume, the camera's (the sun's, the moon's, the sky's, the ground's), whether the moon
     *  lights anything, the ambient's tint and the night's own light. (No camera position: the camera sits at the origin
     *  of render space.) */
    int _locSunDirection;
    int _locMoonDirection;
    int _locSkyLightPerHeight;
    int _locSunLight;
    int _locMoonLight;
    int _locSkyIrradiance;
    int _locGroundIrradiance;
    int _locIsMoonLit;
    int _locAmbientTint;
    int _locNightLight;
    /** @brief Screen-space AO: the AO map, its active flag and the scene resolution used to map gl_FragCoord to the AO
     *  map's UV. */
    int _locAmbientOcclusionMap;
    int _locAoMapActive;
    int _locAoResolution;
    /** @brief The cascades, the light they were rendered from and how much they shadow the sun and the moon. */
    int _locLightVP;
    int _locShadowMap;
    int _locLightVPFar;
    int _locShadowMapFar;
    int _locShadowFarActive;
    int _locShadowStrength;
    int _locShadowBias;
    int _locShadowTexelSize;
    int _locShadowLightDirection;
    int _locSunShadowFade;
    int _locMoonShadowFade;
    /** @brief The point-light (cube) shadow. */
    int _locPointShadowActive;
    int _locPointShadowLightPos;
    int _locPointShadowLightRadiance;
    int _locPointShadowLightRange;
    int _locPointShadowCube;
    int _locPointShadowFar;
    int _locPointShadowBias;
    /** @brief The forward point lights, uploaded per drawn object. */
    int _locPointLightCount;
    int _locPointLightPositions;
    int _locPointLightRadiances;
    int _locPointLightRanges;
    /** @brief The haze: the scene's view-projection, where the volume's screen directions lie (uploaded per
     *  frame), and the draw's haze multiplier (its placement's haze, set before every lit draw:
     *  WorldRendererLighting_SetDrawHaze). */
    int _locHazeViewProjection;
    int _locHazeMultiplier;
    /** @brief The night vision: the field luminance, the calibration (uploaded once at load), the strength and the rods'
     *  grey. */
    int _locNightVisionFieldLuminance;
    int _locNightVisionHalfLuminance;
    int _locNightVisionSteepness;
    int _locNightVisionStrength;
    int _locNightVisionTint;
    /** @brief The atmosphere (common/atmosphere): the air and the height the sky is seen from, which the haze's volume
     *  reads take. */
    WorldRendererAtmosphereLocations _atmosphere;
    /** @brief The haze (common/haze): the volume on WorldRendererTextureUnit_HazeVolume and the frame's haze. */
    WorldRendererHazeLocations _haze;
} WorldRendererLitLocations;

/** @brief One PBR program that shades model objects, and its uniform locations: the rigid one (world_pbr) or the skinned
 *  one (world_pbr_skinned: world_pbr's fragment stage over a vertex stage that skins). */
typedef struct WorldRendererPbrProgramStruct
{
    /** @brief The shader asset (held under the shader user); NULL = unavailable. */
    GameShader* _shader;
    /** @brief The renderer's own copy of the shader's location table: the asset's table with the material map
     *  samplers pointed at mraMap/normalMap/emissiveMap, so DrawMesh binds each material's maps without the
     *  renderer writing into the shared shader asset. */
    int _locs[RL_MAX_SHADER_LOCATIONS];
    /** @brief The shader to draw PBR meshes with: the asset's program with _locs as its location table. */
    Shader _passShader;
    /** @brief The shader's frame-lighting uniform locations (common/lighting). */
    WorldRendererLitLocations _lit;
    /** @brief Its dither uniforms, uploaded per object (every mesh of a model shares its fade). */
    WorldRendererDitherLocations _dither;
    /** @brief Cached per-material scalar-factor and map-presence uniform locations (-1 when absent), uploaded
     *  per material by the mesh draw loop. */
    int _locMetallic;
    int _locRoughness;
    int _locAo;
    int _locEmissiveColor;
    int _locEmissiveIntensity;
    int _locHasMraMap;
    int _locHasNormalMap;
    int _locHasEmissiveMap;
    /** @brief The skinned program's bone palette (location -1 in the rigid program's). */
    WorldRendererPalette _palette;
} WorldRendererPbrProgram;

/** @brief The PBR programs that shade model objects, and this render's forward lights. */
typedef struct WorldRendererPbrStateStruct
{
    /** @brief The program for rigid meshes; its shader NULL = PBR unavailable (models draw unlit). */
    WorldRendererPbrProgram _rigid;
    /** @brief The program for skinned meshes; its shader NULL = skinned meshes draw with the rigid one, in their
     *  bind pose. Every lit upload of the rigid program goes to it too. */
    WorldRendererPbrProgram _skinned;
    /** @brief The program for instanced copies (world_pbr_instanced: world_pbr's fragment stage over a vertex stage
     *  that places every copy of a cell); its shader NULL = copies draw one at a time with the rigid one. Every lit
     *  upload of the rigid program goes to it too; its instancing uniforms are in WorldRendererInstanceState. */
    WorldRendererPbrProgram _instanced;
    /** @brief This render's forward-light candidates (WorldCullingLight), which every lit shader (models and sprites)
     *  picks each object's lights from: every light of the render cache except the cube-shadow light, which is shaded on
     *  its own. */
    GenericBuffer _forwardLights;
    /** @brief The candidates' linear radiance (WorldRendererRadiance), parallel to _forwardLights. */
    GenericBuffer _forwardRadiances;
} WorldRendererPbrState;

/** @brief The unlit fallback's skinned shader. Without the PBR shader models draw with their materials' own shaders
 *  (raylib's default), and skinned meshes with this one, which shades as raylib's default does. */
typedef struct WorldRendererUnlitStateStruct
{
    /** @brief The shader (world_unlit_skinned) and its palette; NULL = skinned meshes draw their bind pose unlit. */
    GameShader* _skinnedShader;
    WorldRendererPalette _skinnedPalette;
} WorldRendererUnlitState;

/** @brief Reversed depth for the camera's passes (GLDepth.h). */
typedef struct WorldRendererDepthStateStruct
{
    /** @brief The GL functions and whether the GPU has clip control (loaded with the shaders). */
    GLDepthApi _api;
    /** @brief Whether the camera's passes draw reversed: the API is available. Without it they keep raylib's projection
     *  and its far plane (RL_CULL_DISTANCE_FAR), logged once. */
    bool _isReversed;
    /** @brief Set once the fallback has been logged. */
    bool _isFailureReported;
} WorldRendererDepthState;

/** @brief The eye's adaptation to the picture (WorldRendererExposure.c; the eye's model is EyeAdaptation.h's). */
typedef struct WorldRendererExposureStateStruct
{
    /** @brief The eye: the level it is adapted to and the one it eases toward. */
    EyeAdaptation _eye;
    /** @brief The meter's shader (world_luminance; NULL: unavailable) and its uniforms' locations: the window, and the
     *  scene's depth that tells the sky apart with its switch and its convention (common/depth). */
    GameShader* _shader;
    int _locWindow;
    int _locDepthTexture;
    int _locDepthMask;
    int _locDepthReversed;
    /** @brief The meter's target (one float a block, EYE_ADAPTATION_METER_WIDTH x EYE_ADAPTATION_METER_HEIGHT) and whether
     *  it exists. */
    RenderTexture2D _target;
    bool _hasTarget;
    /** @brief The readback's GL functions and its copies of the target in flight. */
    GLReadbackApi _readbackApi;
    GLReadback _readback;
    /** @brief Whether the meter runs (its shader, the readback's functions, the target and its buffers are all there);
     *  without it the eye adapts to the anchor (the sky's light at the camera). */
    bool _isAvailable;
    /** @brief The newest blocks read back (log2 luminances, rows from the bottom). */
    float _blocks[EYE_ADAPTATION_METER_BLOCK_COUNT];
    /** @brief Set once the meter's failure has been logged. */
    bool _isFailureReported;
} WorldRendererExposureState;

/** @brief The tonemap pass (HDR scene -> display, adds bloom and sun shafts). */
typedef struct WorldRendererTonemapStateStruct
{
    /** @brief The tonemap shader; NULL = unavailable (the HDR scene is blitted untonemapped). */
    GameShader* _shader;
    /** @brief Cached tonemap uniform locations (-1 when absent). */
    int _locExposure;
    int _locBloomTexture;
    int _locBloomStrength;
    int _locSunshaftTexture;
    int _locSunshaftStrength;
} WorldRendererTonemapState;

/**
 * @brief A sky texture the world's environment names (the Milky Way, the moon's face).
 *
 * Each slot holds its texture under its own asset user, so replacing one slot's texture can never release another's
 * (two slots may name the same asset), and loads only when the environment names another texture: a failed name is
 * logged once and not retried until the name changes.
 */
typedef struct WorldRendererSkyTextureStruct
{
    /** @brief The texture, held under _user; NULL = unavailable (not loaded yet, or the name failed). */
    GameTexture* _texture;
    /** @brief The asset user holding _texture; minted on the first load, retired with the renderer. */
    AssetUserID _user;
    /** @brief The name _texture was loaded (or tried) for, NUL-terminated (unsigned char); valid while _isSynced. */
    GenericBuffer _name;
    /** @brief Whether a name has been tried. */
    bool _isSynced;
} WorldRendererSkyTexture;

/** @brief The atmospheric sky pass, with the moon, the Milky Way and light pollution's skyglow. */
typedef struct WorldRendererSkyStateStruct
{
    /** @brief The atmospheric sky shader drawn behind the geometry; NULL = fall back to the gradient clear. */
    GameShader* _shader;
    /** @brief The Milky Way panorama the world names (MilkyWayTexture); without it, no Milky Way. */
    WorldRendererSkyTexture _milkyWay;
    /** @brief The moon's face the world names (MoonTexture); without it the disc is a flat grey. */
    WorldRendererSkyTexture _moon;
    /** @brief Cached world_sky uniform locations (-1 when absent). */
    int _locResolution;
    int _locInvViewProj;
    int _locSunDirection;
    int _locSunIntensity;
    int _locSunDiscIntensity;
    int _locSunSize;
    int _locSkyTint;
    int _locSkyglowRadiance;
    int _locMilkyWayTexture;
    int _locWorldToGalactic;
    int _locMilkyWayRadiance;
    int _locMilkyWayLod;
    int _locMoonDirection;
    int _locMoonRight;
    int _locMoonUp;
    int _locMoonCosRadius;
    int _locMoonSinRadius;
    int _locMoonColor;
    int _locMoonDiscRadiance;
    int _locMoonEarthshine;
    int _locMoonOpacity;
    int _locMoonSkyIntensity;
    int _locHasMoonTexture;
    int _locMoonTexture;
    int _locNightVisionFieldLuminance;
    int _locNightVisionHalfLuminance;
    int _locNightVisionSteepness;
    int _locNightVisionStrength;
    int _locNightVisionTint;
    /** @brief The atmosphere (common/atmosphere): the air and the tables. */
    WorldRendererAtmosphereLocations _atmosphere;
    /** @brief The haze (common/haze): the ground haze in front of the sky, seen through the volume's air. */
    WorldRendererHazeLocations _haze;
} WorldRendererSkyState;

/**
 * @brief The atmosphere's tables (WorldRendererAtmosphere.c), the aerial-perspective volume and the sky's
 *        irradiance table: their shaders and render targets, the air they were built for, the C mirror's own
 *        tables (WorldSkyLight.h), the A/B switch and the volume's debug view.
 *
 * The transmittance (WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH x _HEIGHT) and multiple scattering
 * (WORLD_ATMOSPHERE_MULTISCATTER_SIZE squared) tables are built when the air changes, the irradiance table
 * (WORLD_ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH x _HEIGHT) after them or when the haze's forward scattering or the share of
 * multiple scattering changes; the sky view (two halves of WORLD_ATMOSPHERE_SKY_VIEW_WIDTH x _HEIGHT) and the volume
 * (WORLD_ATMOSPHERE_VOLUME_WIDTH x _HEIGHT) every frame. All RGBA16F, bilinear, clamped.
 */
typedef struct WorldRendererAtmosphereStateStruct
{
    /** @brief The table builders (world_atmosphere_transmittance, _multiscatter, _skyview) and their atmosphere
     *  locations; any NULL = the tables are unavailable (the sky is marched per pixel, and nothing is hazed). */
    GameShader* _transmittanceShader;
    WorldRendererAtmosphereLocations _transmittanceLocations;
    GameShader* _multiscatterShader;
    WorldRendererAtmosphereLocations _multiscatterLocations;
    int _multiscatterLocGroundAlbedo;
    GameShader* _skyViewShader;
    WorldRendererAtmosphereLocations _skyViewLocations;
    int _skyViewLocLightCosZenith;
    int _skyViewLocLightIntensity;
    int _skyViewLocMultipleScattering;
    int _skyViewLocTableHalf;
    int _skyViewLocLightPollution;
    int _skyViewLocSkyTint;
    int _skyViewLocHazeDistanceScale;
    int _skyViewLocHazeStartDistance;
    int _skyViewLocGroundAlbedo;
    int _skyViewLocAmbientTint;
    int _skyViewLocGroundNightLight;
    /** @brief The volume's builder (world_atmosphere_volume), its atmosphere locations and its own; NULL = no haze. */
    GameShader* _volumeShader;
    WorldRendererAtmosphereLocations _volumeLocations;
    int _volumeLocInvViewProjection;
    int _volumeLocSunDirection;
    int _volumeLocSunIntensity;
    int _volumeLocMoonDirection;
    int _volumeLocMoonIntensity;
    int _volumeLocMultipleScattering;
    int _volumeLocLightPollution;
    int _volumeLocSkyTint;
    int _volumeLocGroundHazeBaseHeight;
    int _volumeLocGroundAlbedo;
    /** @brief The irradiance table's builder (world_atmosphere_irradiance), its atmosphere locations and its own;
     *  NULL = the tables are unavailable. */
    GameShader* _irradianceShader;
    WorldRendererAtmosphereLocations _irradianceLocations;
    int _irradianceLocMultipleScattering;
    /** @brief The tables' targets and the volume's (colour only) and whether each was created; made once, with the
     *  shaders. */
    RenderTexture2D _transmittance;
    bool _hasTransmittance;
    RenderTexture2D _multiscatter;
    bool _hasMultiscatter;
    RenderTexture2D _skyView;
    bool _hasSkyView;
    RenderTexture2D _volume;
    bool _hasVolume;
    RenderTexture2D _irradiance;
    bool _hasIrradiance;
    /** @brief What the volume's debug view shows (WorldRenderer_SetHazeView: off by default) and which slice. */
    WorldRendererHazeView _view;
    int _viewSlice;
    /** @brief What the transmittance and multiple scattering tables hold, and how often they were built (the status
     *  line); what the irradiance table holds (WorldAtmosphere_UpdateIrradianceRecord). */
    WorldAtmosphereTableRecord _tables;
    WorldAtmosphereTableRecord _irradianceTable;
    /** @brief The C mirror's tables (made with the renderer, their sets on the heap): the set the sky's light at the
     *  camera reads (WorldRendererLighting_PrepareSkyLight) and a second one a rebuild fills over frames when the air
     *  changes (WorldRendererAtmosphere_UpdateSkyLightTables). Built whole when a world is prepared, or on the first
     *  frame. */
    WorldSkyLightBuilder _skyLight;
    /** @brief Whether the tables draw the sky and the haze (WorldRenderer_SetAtmosphereTablesEnabled: the A/B; on by
     *  default). */
    bool _isEnabled;
    /** @brief Set once a target that could not be made has been logged. */
    bool _isTargetFailureReported;
} WorldRendererAtmosphereState;

/** @brief The star field: the seeded star catalogue as one mesh of one-pixel quads, drawn right after the sky. */
typedef struct WorldRendererStarStateStruct
{
    /** @brief The star shader; NULL = no stars. */
    GameShader* _shader;
    /** @brief Cached world_stars uniform locations (-1 when absent). */
    int _locStarsToWorld;
    int _locViewProjection;
    int _locResolution;
    int _locLimitingMagnitude;
    int _locRadianceScale;
    int _locTwinkleStrength;
    int _locTimeSeconds;
    int _locMoonDirection;
    int _locMoonCosRadius;
    /** @brief The camera's depth convention (common/depth): where the stars sit in depth. */
    int _locDepthReversed;
    /** @brief The atmosphere (common/atmosphere): the transmittance table dims and reddens the stars near the
     *  horizon. */
    WorldRendererAtmosphereLocations _atmosphere;
    /** @brief The haze (common/haze): the ground haze dims the stars. */
    WorldRendererHazeLocations _haze;
    /** @brief The catalogue's mesh, uploaded once per catalogue: six vertices per star and no index buffer (raylib's
     *  16-bit indices would cap an indexed mesh at 16384 four-vertex stars). Its CPU arrays are freed after the
     *  upload. Valid while _hasMesh. */
    Mesh _mesh;
    bool _hasMesh;
    /** @brief The seed and star count the current catalogue was built for, and whether one was built at all. The
     *  catalogue depends on the density only through the count. */
    uint64_t _builtSeed;
    size_t _builtCount;
    bool _isBuilt;
    /** @brief Stars in the current mesh (0 = none). */
    size_t _starCount;
    /** @brief Set once a catalogue that could not be built has been logged. */
    bool _isBuildFailureReported;
} WorldRendererStarState;

/** @brief One sprite's matrices and place for this frame (WorldRendererSprites_PrepareFrame), parallel to the render
 *  cache's sprites. */
typedef struct WorldRendererSpriteFrameStruct
{
    /** @brief The sprite as the camera sees it (scene, G-buffer and crisp passes): a billboard faces the camera. */
    Matrix CameraTransform;
    /** @brief The sprite as the cascades' light sees it (a billboard faces that light); the camera transform while the
     *  cascades do not run or for a fixed sprite. */
    Matrix CascadeTransform;
    /** @brief The sprite as the cube shadow's light sees it; the camera transform while the cube shadow does not run or
     *  for a fixed sprite. */
    Matrix PointTransform;
    /** @brief Its distance in front of the camera, along the view (the camera-facing quad's centre), for sorting. */
    float Depth;
} WorldRendererSpriteFrame;

/**
 * @brief The sprite pass: the unit quad every sprite draws with, the lit sprite shader, and each frame's sprite
 *        matrices and transparent draw order.
 *
 * The G-buffer and shadow passes draw sprites too, with their own shaders (in their states) and this pass's quad and
 * matrices.
 */
typedef struct WorldRendererSpriteStateStruct
{
    /** @brief The lit sprite shader (world_sprite: lights, shadows, haze, night vision, alpha modes); NULL = sprites
     *  are not drawn at all. */
    GameShader* _shader;
    /** @brief Its frame-lighting uniform locations (common/lighting). */
    WorldRendererLitLocations _lit;
    /** @brief Its dither uniforms, uploaded per sprite. */
    WorldRendererDitherLocations _dither;
    /** @brief Its per-sprite uniform locations (-1 when absent). */
    int _locUvRect;
    int _locShadowModel;
    int _locPointShadowModel;
    int _locDecalOffset;
    int _locAlphaMode;
    int _locAlphaCutoff;
    int _locIsLit;
    int _locEmission;
    int _locLightWrap;
    int _locUseScreenAo;
    /** @brief The unit quad (x, y in [0, 1], normal +Z, UVs with v 0 at the image's top); valid while _hasQuad. */
    Mesh _quad;
    bool _hasQuad;
    /** @brief This frame's WorldRendererSpriteFrame per cached sprite. */
    GenericBuffer _frames;
    /** @brief This frame's blended and additive sprites that are not decals and that the camera's list holds
     *  (SpriteDrawOrder), farthest first. */
    GenericBuffer _transparentOrder;
} WorldRendererSpriteState;

/** @brief The two shadow cascades of the sky's shadow light (the sun by day, the moon on bright nights). */
typedef struct WorldRendererCascadeStateStruct
{
    /** @brief The depth-only shader used for the cascades, and its dither uniforms; NULL = no sun or moon shadows. */
    GameShader* _depthShader;
    WorldRendererDitherLocations _dither;
    /** @brief Its skinned twin (world_depth_skinned), its dither and its palette; NULL = skinned meshes cast the
     *  shadow of their bind pose. */
    GameShader* _skinnedDepthShader;
    WorldRendererDitherLocations _skinnedDither;
    WorldRendererPalette _skinnedPalette;
    /** @brief The alpha-tested depth shader for cutout sprites (world_sprite_depth) and its locations; NULL = sprites
     *  cast no sun or moon shadow. */
    GameShader* _spriteDepthShader;
    int _spriteLocUvRect;
    int _spriteLocAlphaCutoff;
    WorldRendererDitherLocations _spriteDither;
    /** @brief The NEAR cascade (small extent, sharp); its samplable depth texture is in .depth. */
    RenderTexture2D _nearMap;
    /** @brief Whether _nearMap has been created (and its framebuffer is complete). */
    bool _hasNearMap;
    /** @brief The FAR cascade (large extent, coarse) + its creation flag. Sampled by the PBR shader outside the near
     *  cascade so big / distant casters still shadow at range. Optional. (Both cascades' light matrices are the frame's:
     *  WorldRenderFrame.NearCascade and FarCascade.) */
    RenderTexture2D _farMap;
    bool _hasFarMap;
    /** @brief Each cascade's texel grid from frame to frame (WorldRenderSpace_StepCascade: its centre in the world, from
     *  the last frame the cascades ran): the next frame's centre moves from it by whole texels, so the grid stands still
     *  while the light does and turns about the camera, not the world's origin, while the light turns. Zeroed with the
     *  renderer; one pair for the renderer's one camera, like the LOD states. */
    WorldRenderCascadeGrid _nearGrid;
    WorldRenderCascadeGrid _farGrid;
} WorldRendererCascadeState;

/** @brief The cube shadow of the shadow-casting point light. */
typedef struct WorldRendererPointShadowStateStruct
{
    /** @brief The cube-depth shader; NULL = no point-light shadow. Cached uniform locations: the shadow light's
     *  position (render space) and far range, and the dither. */
    GameShader* _shader;
    int _locLightPos;
    int _locLightFar;
    WorldRendererDitherLocations _dither;
    /** @brief Its skinned twin (world_cube_depth_skinned) and the same locations, and its palette; NULL = skinned
     *  meshes cast the shadow of their bind pose. */
    GameShader* _skinnedShader;
    int _skinnedLocLightPos;
    int _skinnedLocLightFar;
    WorldRendererDitherLocations _skinnedDither;
    WorldRendererPalette _skinnedPalette;
    /** @brief The alpha-tested cube-depth shader for cutout sprites (world_sprite_cube_depth) and its locations; NULL =
     *  sprites cast no point-light shadow. */
    GameShader* _spriteShader;
    int _spriteLocUvRect;
    int _spriteLocAlphaCutoff;
    int _spriteLocLightPos;
    int _spriteLocLightFar;
    WorldRendererDitherLocations _spriteDither;
    /** @brief A colour CUBE (RGBA8 packed distance) + its own depth renderbuffer + framebuffer; the shadow light
     *  renders 6 faces into it each frame it is active. */
    unsigned int _cube;
    unsigned int _depthRenderbuffer;
    unsigned int _framebuffer;
    /** @brief Whether the cube resources have been created. (The light and its far range are the frame's:
     *  WorldRenderFrame.PointShadowPosition and PointShadowFar.) */
    bool _hasCube;
} WorldRendererPointShadowState;

/** @brief The normal/flag G-buffer (view-space normals + surface/outline flag) read by the AO and outline passes. */
typedef struct WorldRendererGBufferStateStruct
{
    /** @brief The G-buffer shader for models; NULL = AO and outlines are unavailable. */
    GameShader* _shader;
    /** @brief Cached world_normal outline-flag and haze-multiplier uniform locations (-1 when absent): an outlined
     *  surface's flag holds its haze multiplier. */
    int _locOutlineFlag;
    int _locHazeMultiplier;
    /** @brief The two G-buffer shaders' dither uniforms: their fade stays at the default 0 (the G-buffer draws a fading
     *  object solid below WORLD_VISIBILITY_GBUFFER_FADE_LIMIT and skips it above). */
    WorldRendererDitherLocations _dither;
    WorldRendererDitherLocations _spriteDither;
    /** @brief The model shader's skinned twin (world_normal_skinned), its outline flag, its dither (solid like the
     *  model shader's) and its palette; NULL = skinned meshes draw their bind pose into the G-buffer. */
    GameShader* _skinnedShader;
    int _skinnedLocOutlineFlag;
    int _skinnedLocHazeMultiplier;
    WorldRendererDitherLocations _skinnedDither;
    WorldRendererPalette _skinnedPalette;
    /** @brief The G-buffer shader for cutout sprites (alpha-tested at each sprite's cutoff); NULL = sprites get no
     *  outlines and no AO of their own. */
    GameShader* _spriteShader;
    /** @brief Cached world_normal_sprite uniform locations (-1 when absent): the outline flag, the haze multiplier, the
     *  frame's area and the cutoff. */
    int _spriteLocOutlineFlag;
    int _spriteLocHazeMultiplier;
    int _spriteLocUvRect;
    int _spriteLocAlphaCutoff;
    /** @brief The low-res G-buffer target (RGB = view normal, A = surface/outline flag). Its depth is a samplable
     *  texture (for the AO pre-pass) when the driver allows. */
    RenderTexture2D _target;
    /** @brief Whether _target has been created. */
    bool _hasTarget;
    /** @brief Whether _target's depth is a samplable texture (required for the AO pre-pass). */
    bool _isDepthSamplable;
    /** @brief Set once a target creation failure has been logged. */
    bool _isTargetFailureReported;
} WorldRendererGBufferState;

/** @brief The screen-space ambient-occlusion pre-pass. */
typedef struct WorldRendererAoStateStruct
{
    /** @brief The AO shader (SSAO from the G-buffer -> AO-multiplier texture); NULL = no AO. Its uniform
     *  locations follow. */
    GameShader* _shader;
    int _locResolution;
    int _locInvProjection;
    int _locProjection;
    int _locDepthTexture;
    int _locNormalTexture;
    int _locStrength;
    int _locDepthReversed;
    /** @brief The low-res AO-multiplier target (R = 1 open .. <1 occluded) the PBR pass samples for its ambient
     *  term. */
    RenderTexture2D _target;
    /** @brief Whether _target has been created. */
    bool _hasTarget;
    /** @brief Set once a target creation failure has been logged. */
    bool _isTargetFailureReported;
} WorldRendererAoState;

/** @brief The outline post pass (depth/normal-edge silhouettes and creases). */
typedef struct WorldRendererOutlineStateStruct
{
    /** @brief The outline (postfx) shader; NULL = outlines unavailable. */
    GameShader* _shader;
    /** @brief Cached world_postfx uniform locations (-1 when absent). */
    int _locResolution;
    int _locInvProjection;
    int _locProjection;
    int _locDepthTexture;
    int _locNormalTexture;
    int _locSunDirectionView;
    int _locStrength;
    int _locWorldUpView;
    int _locDepthReversed;
    /** @brief The atmosphere and the haze (common/haze): the outlines fade by the haze's transmittance. */
    WorldRendererAtmosphereLocations _atmosphere;
    WorldRendererHazeLocations _haze;
    /** @brief The target the outline pass writes into (same size/format as the scene target). */
    RenderTexture2D _target;
    /** @brief Whether _target has been created. */
    bool _hasTarget;
    /** @brief Whether outlines are drawn (code-configurable per spec; on by default). */
    bool _isEnabled;
    /** @brief Set once a target creation failure has been logged. */
    bool _isTargetFailureReported;
} WorldRendererOutlineState;

/** @brief The bloom pass (bright-pass prefilter + separable blur). */
typedef struct WorldRendererBloomStateStruct
{
    /** @brief The two bloom shaders; either NULL = bloom unavailable. */
    GameShader* _prefilterShader;
    GameShader* _blurShader;
    /** @brief Cached bloom-prefilter uniform locations (-1 when absent). */
    int _prefilterLocResolution;
    int _prefilterLocThreshold;
    int _prefilterLocSoftKnee;
    int _prefilterLocDepthTexture;
    int _prefilterLocDepthMask;
    int _prefilterLocDepthReversed;
    /** @brief Cached bloom-blur uniform locations (-1 when absent). */
    int _blurLocResolution;
    int _blurLocDirection;
    /** @brief Whether bloom is drawn (code toggle for A/B testing; on by default). */
    bool _isEnabled;
    /** @brief Ping-pong bloom buffers (colour-only HDR, reduced resolution, BILINEAR): the prefilter writes A,
     *  then the separable blur ping-pongs A<->B, leaving the final bloom in A. */
    RenderTexture2D _targetA;
    RenderTexture2D _targetB;
    /** @brief Whether the bloom targets have been created. */
    bool _hasTargets;
    /** @brief Size of the bloom buffers, in pixels. */
    int _width;
    int _height;
    /** @brief Set once a target creation failure has been logged. */
    bool _isTargetFailureReported;
} WorldRendererBloomState;

/** @brief The sun-shaft (god ray) pass. */
typedef struct WorldRendererSunshaftStateStruct
{
    /** @brief The sun-shaft shader; NULL = sun shafts unavailable. */
    GameShader* _shader;
    /** @brief Cached sun-shaft uniform locations (-1 when absent). */
    int _locResolution;
    int _locDepthTexture;
    int _locSunScreenPos;
    int _locSourceLimit;
    int _locDepthReversed;
    /** @brief Whether sun shafts are drawn (code toggle for A/B testing; on by default). */
    bool _isEnabled;
    /** @brief The sun-shaft buffer (colour-only HDR, reduced resolution, BILINEAR). */
    RenderTexture2D _target;
    /** @brief Whether _target has been created. */
    bool _hasTarget;
    /** @brief Size of the sun-shaft buffer, in pixels. */
    int _width;
    int _height;
    /** @brief Set once a target creation failure has been logged. */
    bool _isTargetFailureReported;
} WorldRendererSunshaftState;

/** @brief One part of a terrain chunk's GPU mesh: a raylib mesh of at most 65536 vertices (its indices are unsigned
 *  shorts) with positions, normals and indices, the buffer of its weights in its vertex array (four normalized bytes at
 *  each of the attribute locations 9 to 12) and the buffer of its vertices' flags (four normalized bytes at
 *  location 13: x the face material). Its CPU arrays are freed after the upload, but for the indices: raylib's DrawMesh
 *  draws a mesh without them as an unindexed one. */
typedef struct WorldRendererTerrainPartStruct
{
    Mesh _mesh;
    unsigned int _weightBuffer;
    unsigned int _flagBuffer;
} WorldRendererTerrainPart;

/** @brief A terrain chunk's GPU mesh: its parts (none for a chunk without a surface), and the chunk revision they
 *  were built from (0: not built yet). */
typedef struct WorldRendererTerrainChunkMeshStruct
{
    WorldRendererTerrainPart* _parts;
    uint32_t _partCount;
    uint32_t _revision;
} WorldRendererTerrainChunkMesh;

/** @brief The most rules the terrain shader holds (its rule arrays' size): TerrainAsset.h's TERRAIN_MAX_RULES. */
#define WORLD_RENDERER_TERRAIN_MAX_RULES TERRAIN_MAX_RULES

/** @brief A terrain's material uniforms, worked out once when its record is made (the terrain never changes while
 *  loaded) and uploaded whenever the lit pass turns to its chunks: the arrays' layouts are the shader's. */
typedef struct WorldRendererTerrainUniformsStruct
{
    int _materialCount;
    /** @brief Per palette material, four floats each: the linear tint; (1 / tile size, roughness, normal strength,
     *  height blend strength); (triplanar mode, triplanar slope in degrees, puddles, porosity); the linear mean colour
     *  times the tint. Its tile sizes, for the chunks' wrapped offsets. */
    float _tint[TERRAIN_MAX_MATERIALS * 4U];
    float _surface[TERRAIN_MAX_MATERIALS * 4U];
    float _projection[TERRAIN_MAX_MATERIALS * 4U];
    float _meanColor[TERRAIN_MAX_MATERIALS * 4U];
    double _tileSizes[TERRAIN_MAX_MATERIALS];
    int _ruleCount;
    /** @brief Per rule: (material, covered bits, seed bits, condition bits); (slope min, max, blend, opacity); (height
     *  min, max, blend, 0); (1 / noise cell size, strength, 0, 0). Its noise periods in metres (0 without noise), for the
     *  chunks' wrapped offsets. */
    int _ruleIds[WORLD_RENDERER_TERRAIN_MAX_RULES * 4U];
    float _ruleSlope[WORLD_RENDERER_TERRAIN_MAX_RULES * 4U];
    float _ruleHeight[WORLD_RENDERER_TERRAIN_MAX_RULES * 4U];
    float _ruleNoise[WORLD_RENDERER_TERRAIN_MAX_RULES * 4U];
    double _noisePeriods[WORLD_RENDERER_TERRAIN_MAX_RULES];
} WorldRendererTerrainUniforms;

/** @brief The GPU meshes of one terrain's chunks, one per chunk of the layout revision they were cut for, and its
 *  materials' texture arrays and uniforms, kept while a cache entry resolves the terrain. */
typedef struct WorldRendererTerrainMeshesStruct
{
    /** @brief The terrain (borrowed under the model user: the meshes go with it, WorldRendererTerrain_ReleaseMeshes). */
    const TerrainAsset* _asset;
    uint32_t _layoutRevision;
    WorldRendererTerrainChunkMesh* _chunks;
    uint32_t _chunkCount;
    /** @brief Set while a cache entry resolves the terrain this frame. */
    bool _isUsed;
    /** @brief Its texture arrays, one per TerrainMaterialMapKind (GL names), and each one's layer size; all 0 until
     *  they are built, and when they could not be (then the terrain shows its materials' mean colours). */
    unsigned int _arrays[TerrainMaterialMapKind_Count];
    int32_t _arrayWidths[TerrainMaterialMapKind_Count];
    int32_t _arrayHeights[TerrainMaterialMapKind_Count];
    /** @brief Whether every array was built, and whether building them was tried (once per record). */
    bool _hasArrays;
    bool _isArrayBuildTried;
    /** @brief Its material uniforms. */
    WorldRendererTerrainUniforms _uniforms;
} WorldRendererTerrainMeshes;

/** @brief The terrain: the lit terrain shader, the chunks' GPU meshes and the chunk key; the texture arrays' GL
 *  functions, the material and rule uniforms and the debug view. */
typedef struct WorldRendererTerrainStateStruct
{
    /** @brief The lit terrain shader (world_terrain); NULL = the chunks draw unlit with raylib's default shader. Its frame
     *  lighting locations, its dither and the chunk key's colour (chunkTint). */
    GameShader* _shader;
    WorldRendererLitLocations _lit;
    WorldRendererDitherLocations _dither;
    int _locChunkTint;
    /** @brief The materials' and rules' uniforms: per frame the view; per terrain its materials, its rules and
     *  whether it has arrays; per chunk its wrapped offsets and height. -1 for those the shader lacks. */
    int _locTerrainView;
    int _locMaterialCount;
    int _locHasMaterialArrays;
    int _locMaterialTint;
    int _locMaterialSurface;
    int _locMaterialProjection;
    int _locMaterialMeanColor;
    int _locRuleCount;
    int _locRuleIds;
    int _locRuleSlope;
    int _locRuleHeight;
    int _locRuleNoise;
    int _locMaterialUvOffset;
    int _locRuleNoiseOffset;
    int _locChunkHeight;
    /** @brief The texture arrays' GL functions (resolved when the shader loads; unavailable: logged once, and the terrains
     *  show their materials' mean colours). */
    GLTextureArrayApi _arrayApi;
    /** @brief Set once the arrays' unavailability, and once a terrain's arrays failing, has been logged. */
    bool _isArrayApiFailureReported;
    bool _isArrayFailureReported;
    /** @brief WorldRendererTerrainMeshes, one per terrain a cache entry resolves. */
    GenericBuffer _meshes;
    /** @brief Whether the chunk key is on: each chunk tinted by its cell, and the terrain's wireframe. */
    bool _isDebugEnabled;
    /** @brief What the terrains show (WorldRenderer_SetTerrainView). */
    WorldRendererTerrainView _view;
    /** @brief Whether a terrain the camera's pass drew this frame had its arrays (the status's "terrain materials"). */
    bool _wereArraysDrawn;
} WorldRendererTerrainState;

/**
 * @brief The per-draw uniforms of an instanced program (shaders/common/instancing.glsl): the LOD a draw draws, the
 *        component's LOD bands, fade, opacity and bounds this frame, and the LOD's import transform. -1 for a uniform
 *        the shader lacks.
 *
 * Filled by the instancing pass at load, which also uploads the program's pass (instancePass: each program draws one).
 */
typedef struct WorldRendererInstancingLocationsStruct
{
    int _locLod;
    int _locLodCount;
    int _locForcedLod;
    int _locBandStart;
    int _locBandEnd;
    int _locZoomFactor;
    int _locDrawDistance;
    int _locFadeStart;
    int _locFadeBand;
    int _locObjectOpacity;
    int _locBoundsCenter;
    int _locBoundsRadius;
    int _locLodTransform;
} WorldRendererInstancingLocations;

/** @brief One cache entry's records on the GPU: one vertex buffer per build of its cells (each cell a range of it),
 *  uploaded once. */
typedef struct WorldRendererInstanceBufferStruct
{
    /** @brief The build it holds (WorldRenderInstances.CellsBuildId: unique for the cache's life, so a buffer never
     *  outlives its records). */
    uint64_t _buildId;
    /** @brief The GL vertex buffer (WorldInstanceRecord after WorldInstanceRecord) and how many records it holds. */
    unsigned int _buffer;
    size_t _recordCount;
    /** @brief Set while a cache entry holds its build this frame. */
    bool _isUsed;
} WorldRendererInstanceBuffer;

/**
 * @brief The model instances components' copies: the instanced twins of the G-buffer and shadow shaders (the lit
 *        one is _pbr._instanced), the records' GPU buffers and the A/B toggle.
 *
 * A pass whose twin is missing, and every pass while instancing is off, draws each copy on its own through its ordinary
 * shader, with the copy's values from WorldInstancing.c (the C mirror of common/instancing).
 */
typedef struct WorldRendererInstanceStateStruct
{
    /** @brief The lit twin's instancing uniforms (its program is _pbr._instanced). */
    WorldRendererInstancingLocations _pbrInstancing;
    /** @brief The G-buffer's twin (world_normal_instanced), its outline flag and its uniforms; NULL = copies draw one at a
     *  time into the G-buffer. */
    GameShader* _normalShader;
    int _normalLocOutlineFlag;
    int _normalLocHazeMultiplier;
    WorldRendererDitherLocations _normalDither;
    WorldRendererInstancingLocations _normalInstancing;
    /** @brief The cascades' twin (world_depth_instanced); NULL = copies cast one at a time into the cascades. */
    GameShader* _depthShader;
    WorldRendererDitherLocations _depthDither;
    WorldRendererInstancingLocations _depthInstancing;
    /** @brief The cube shadow's twin (world_cube_depth_instanced) and its light's locations; NULL = copies cast one at a
     *  time into the cube. */
    GameShader* _cubeDepthShader;
    int _cubeLocLightPos;
    int _cubeLocLightFar;
    WorldRendererDitherLocations _cubeDither;
    WorldRendererInstancingLocations _cubeInstancing;
    /** @brief WorldRendererInstanceBuffer, one per build a cache entry holds, roughly in the entries' order. */
    GenericBuffer _buffers;
    /** @brief This frame's GL buffer per cache entry (unsigned int, parallel to WorldRenderCache_GetInstances; 0 for an
     *  entry without records). */
    GenericBuffer _entryBuffers;
    /** @brief Whether the copies draw instanced (the default) or one at a time through the ordinary shaders
     *  (WorldRenderer_SetInstancingEnabled: the A/B). */
    bool _isEnabled;
    /** @brief The last render's zoom factor and draw distance scale, for the status's copies by LOD
     *  (WorldRenderer_LogStatus evaluates them with the C mirror). */
    float _lastZoomFactor;
    float _lastDrawDistanceScale;
    /** @brief Whether the camera's pass drew a cell instanced this frame (the status's "instancing"). */
    bool _wereCellsDrawnInstanced;
    /** @brief Set once a buffer raylib could not make, and once a mesh without a vertex array, has been logged. */
    bool _isUploadFailureReported;
    bool _isVertexArrayFailureReported;
} WorldRendererInstanceState;

/** @brief The crisp overlay (un-pixelated OmitPixelation objects composited over the pixelated frame). */
typedef struct WorldRendererCrispStateStruct
{
    /** @brief The crisp-composite shader; NULL = unavailable (flagged objects render pixelated instead). */
    GameShader* _compositeShader;
    /** @brief Cached crisp-composite uniform locations (-1 when absent). */
    int _locCrispDepth;
    int _locSceneDepth;
    int _locExposure;
    int _locDepthReversed;
    /** @brief Whether the crisp overlay is drawn (code toggle for A/B testing; on by default). */
    bool _isEnabled;
    /** @brief The overlay target (HDR colour + samplable depth) at WINDOW resolution. */
    RenderTexture2D _target;
    /** @brief Whether _target has been created (it always has samplable depth). */
    bool _hasTarget;
    /** @brief Size of _target, in pixels (the window / frame target size). */
    int _width;
    int _height;
    /** @brief Set once a target creation failure has been logged. */
    bool _isTargetFailureReported;
} WorldRendererCrispState;

struct WorldRendererStruct
{
    /** @brief Asset manager used to resolve shaders and models; borrowed. */
    AssetManager* _assetManager;
    /** @brief Logger for diagnostics; borrowed, may be NULL. */
    Logger* _logger;
    /** @brief Game config supplying the config-side effect multipliers; borrowed, may be NULL (defaults to 1x). */
    const GameConfig* _config;
    /** @brief Holds the pass shaders for the renderer's lifetime. */
    AssetUserID _shaderUser;
    /** @brief Holds the world's models; replaced by a fresh user in WorldRenderer_PrepareWorld so models no
     *  object uses any more are released. */
    AssetUserID _modelUser;
    /** @brief Whether the debug reference grid is drawn. */
    bool _drawDebugGrid;
    /** @brief Whether the debug light gizmos (wireframe spheres at light positions) are drawn. Default on. */
    bool _lightGizmosEnabled;
    /** @brief The collision world whose debug shapes are drawn as wireframes (borrowed), or NULL (the default). */
    const WorldCollision* _collisionDebug;
    /** @brief Whether the final pixelation pass is applied. */
    bool _pixelationEnabled;
    /** @brief Set once the shaders' load has been attempted (success or failure), so it is tried only once. */
    bool _shadersLoadAttempted;
    /** @brief Master toggle for AO + outlines; when false the scene blits straight to the tonemap. On by default. */
    bool _postfxEnabled;
    /** @brief Toggle for the eye's night vision (A/B; on by default); the world's NightVisionStrength scales it. */
    bool _isNightVisionEnabled;
    /** @brief Toggle for sprite lighting (A/B; on by default): off draws every sprite unlit at its full colours. */
    bool _isSpriteLightingEnabled;
    /** @brief The night vision's calibration on the game's light scale (NightVision_GetCalibration), computed once. */
    NightVisionCalibration _nightVision;
    /** @brief The per-object render cache and this render's model/sprite/light lists. */
    WorldRenderCache _cache;
    /** @brief This render's visibility: per pass the cache entries it draws (the camera's frustum, each cascade's box,
     *  the cube's reach, the draw distances and opacities), per entry its fades. Built right after the cache's sync. */
    WorldVisibility _visibility;
    /** @brief Toggle for the camera's frustum culling and the cascades' box culling (A/B; on by default). Off draws
     *  everything within its draw distance. */
    bool _isCullingEnabled;
    /** @brief Whether every model's LOD is shown by a colour multiplied into its albedo (a debug aid; off by default):
     *  LOD 0 white, 1 green, 2 yellow, 3 red, coarser magenta. */
    bool _isLodTintingEnabled;
    /** @brief What the most recent render did (WorldRenderer_GetStatus). */
    WorldRendererStatus _status;
    /** @brief Set once the status has been logged after the first render. */
    bool _isStatusReported;
    /** @brief The render clock behind WorldRenderFrame.TimeSeconds: seconds of render time, wrapped every hour. */
    double _renderTimeSeconds;

    /** @brief Per-pass state (shader, uniform locations, targets, toggles). */
    WorldRendererSceneState _scene;
    WorldRendererPbrState _pbr;
    WorldRendererUnlitState _unlit;
    WorldRendererExposureState _exposure;
    WorldRendererDepthState _depth;
    WorldRendererTonemapState _tonemap;
    WorldRendererAtmosphereState _atmosphere;
    WorldRendererSkyState _sky;
    WorldRendererStarState _stars;
    WorldRendererSpriteState _sprites;
    WorldRendererCascadeState _cascades;
    WorldRendererPointShadowState _pointShadow;
    WorldRendererGBufferState _gbuffer;
    WorldRendererAoState _ao;
    WorldRendererOutlineState _outline;
    WorldRendererBloomState _bloom;
    WorldRendererSunshaftState _sunshaft;
    WorldRendererCrispState _crisp;
    WorldRendererTerrainState _terrain;
    WorldRendererInstanceState _instances;
};


// WorldRendererCommon.c
/** @brief Config-side shadow strength multiplier (1.0 when no config is bound). */
float WorldRendererCommon_ConfigShadowStrength(const WorldRenderer* self);

/** @brief Config-side ambient-occlusion strength multiplier (1.0 when no config is bound). */
float WorldRendererCommon_ConfigAoStrength(const WorldRenderer* self);

/** @brief Config-side fog strength multiplier (1.0 when no config is bound). */
float WorldRendererCommon_ConfigFogStrength(const WorldRenderer* self);

/** @brief Config-side bloom strength multiplier (1.0 when no config is bound). */
float WorldRendererCommon_ConfigBloomStrength(const WorldRenderer* self);

/** @brief Config-side sun-shaft strength multiplier (1.0 when no config is bound). */
float WorldRendererCommon_ConfigSunshaftStrength(const WorldRenderer* self);

/** @brief Config-side draw distance scale (the config's draw_distance_scale; 1.0 when no config is bound). */
float WorldRendererCommon_ConfigDrawDistanceScale(const WorldRenderer* self);

/** @brief Config-side LOD distance scale (the config's lod_distance_scale; 1.0 when no config is bound). */
float WorldRendererCommon_ConfigLodDistanceScale(const WorldRenderer* self);

/**
 * @brief Caches a geometry shader's dither uniforms (shaders/common/dither.glsl) and uploads their defaults: solid (fade
 *        0), no LOD cross-fade (progress 0, not the incoming LOD), a pixel scale of 1. Call once at load; passes then
 *        change only what differs.
 * @param shader The shader (its program).
 * @param outLocations [out] Receives the locations (-1 for a uniform the shader lacks).
 */
void WorldRendererCommon_PrepareDither(Shader shader, WorldRendererDitherLocations* outLocations);

/**
 * @brief Uploads a draw's dither: its fade (0 solid .. 1 gone) and its part of a LOD cross-fade. Every draw sets all of
 *        it (the uniforms stay with the shader until changed).
 * @param shader The shader (its program).
 * @param locations Its dither locations (WorldRendererCommon_PrepareDither).
 * @param fade The draw's fade (its draw distance, its opacity).
 * @param lodProgress The LOD cross-fade's progress: the share of the pixels the incoming LOD takes (WorldLodDraw); 0 for a
 *        draw that is not part of a cross-fade.
 * @param isIncomingLod Whether the draw is the incoming LOD of a cross-fade (it keeps the pixels the outgoing LOD leaves);
 *        false for the outgoing LOD and for every other draw.
 */
void WorldRendererCommon_SetDither(Shader shader, const WorldRendererDitherLocations* locations, float fade,
    float lodProgress, bool isIncomingLod);

/**
 * @brief Uploads a pass's dither pixel scale: its pixels per scene pixel (1 at the scene's resolution, window / scene in
 *        the crisp overlay), so the pattern sits on the scene's pixel grid in every pass.
 * @param shader The shader (its program).
 * @param locations Its dither locations (WorldRendererCommon_PrepareDither).
 * @param pixelScale The pass's pixels per scene pixel, per axis.
 */
void WorldRendererCommon_SetDitherPixelScale(Shader shader, const WorldRendererDitherLocations* locations,
    Vector2 pixelScale);

/**
 * @brief Finds a skinned shader's bone palette (bonePalette, shaders/common/skinning.glsl): its location and how many
 *        bones its array holds, read from the program. Call once at load.
 * @param shader The shader (its program).
 * @param outPalette [out] Receives the palette, holding no pose (location -1 and no bones for a shader without one).
 */
void WorldRendererCommon_PreparePalette(Shader shader, WorldRendererPalette* outPalette);

/**
 * @brief Uploads a pose's skinning palette into a skinned shader, unless the shader holds it already (the same pose at
 *        the same revision since the frame began).
 * @param shader The shader (its program).
 * @param palette Its palette (WorldRendererCommon_PreparePalette).
 * @param pose The pose; must not be NULL.
 * @returns Whether the shader holds the pose's palette now: false when it has no palette or the pose has more bones
 *          than it holds (the object's skinned meshes then draw in their bind pose).
 */
bool WorldRendererCommon_UploadPalette(Shader shader, WorldRendererPalette* palette, const WorldAnimatorPose* pose);

/**
 * @brief Converts an 8-bit sRGB colour to linear light in [0,1]^3 (alpha ignored).
 * @param color The sRGB colour.
 * @param outRGB [out] Receives the linear red, green and blue.
 */
void WorldRendererCommon_ColorToLinear(Color color, float outRGB[3]);

/**
 * @brief Loads a shader asset by name under the renderer's shader user.
 *
 * A failure is logged once (warning) and the feature that needs the shader stays off.
 * @param self The renderer.
 * @param assetName The shader asset's name.
 * @returns The shader (held until the renderer is destroyed), or NULL when it is unavailable.
 */
GameShader* WorldRendererCommon_LoadShader(WorldRenderer* self, const unsigned char* assetName);

/**
 * @brief Loads a texture asset by name under @p user.
 *
 * A failure is logged (warning); callers load each name once, so it is logged once, and the feature that needs the
 * texture goes without it.
 * @param self The renderer.
 * @param user The asset user to hold the texture.
 * @param assetName The texture asset's name.
 * @param featureName What goes without it, for the warning (e.g. u8"the Milky Way is not drawn").
 * @returns The texture (held until @p user releases it), or NULL when it is unavailable.
 */
GameTexture* WorldRendererCommon_LoadTexture(WorldRenderer* self, AssetUserID user, const unsigned char* assetName,
    const unsigned char* featureName);

/**
 * @brief Logs a warning (no-op without a logger). For failures that can only happen once, such as at shader load.
 * @param self The renderer.
 * @param message The warning.
 */
void WorldRendererCommon_Warn(WorldRenderer* self, const unsigned char* message);

/**
 * @brief Logs a warning the first time it is called for @p isReported, then never again for it.
 * @param self The renderer (its logger may be NULL).
 * @param isReported The once-flag for this message; set to true.
 * @param message The warning.
 */
void WorldRendererCommon_WarnOnce(WorldRenderer* self, bool* isReported, const unsigned char* message);

/**
 * @brief Makes a camera pass's depth buffer: 32-bit float through GLDepth while the renderer draws reversed, raylib's
 *        own (the driver's choice, usually 24-bit) otherwise.
 * @param self The renderer.
 * @param width Its width in pixels.
 * @param height Its height.
 * @param samplable Whether later passes read it: a texture; otherwise a renderbuffer.
 * @returns Its GL name, 0 when it could not be made (logged once). Attach it with rlFramebufferAttach; raylib's
 *          UnloadRenderTexture deletes it with its framebuffer.
 */
unsigned int WorldRendererCommon_LoadCameraDepth(WorldRenderer* self, int width, int height, bool samplable);

/**
 * @brief Tries to create an HDR (RGBA16F) framebuffer with a camera pass's depth attachment
 *        (WorldRendererCommon_LoadCameraDepth).
 *
 * The depth is a samplable texture when @p samplableDepth is true, or a renderbuffer otherwise (depth testing
 * is identical either way).
 * @returns The target, or a zero target (id 0) if the GPU cannot make the combination complete.
 */
RenderTexture2D WorldRendererCommon_TryCreateHdrTarget(WorldRenderer* self, int width, int height, bool samplableDepth);

/**
 * @brief Sets a depth reader's convention (shaders/common/depth.glsl's depthReversed): 1 while the frame draws reversed
 *        depth, 0 otherwise.
 * @param shader The reader's shader.
 * @param location Its depthReversed location (-1: nothing).
 * @param frame The frame (IsDepthReversed).
 */
void WorldRendererCommon_SetDepthConvention(Shader shader, int location, const WorldRenderFrame* frame);

/**
 * @brief Starts a camera pass's depth: reversed depth while the frame draws it (GLDepth_BeginReversed: depth
 *        cleared to 0, the greater depth passing), nothing otherwise. Call after BeginTextureMode, before the clear.
 * @param self The renderer.
 * @param frame The frame (IsDepthReversed).
 */
void WorldRendererCommon_BeginCameraDepth(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Loads the frame's projection after BeginMode3D while the frame draws reversed (raylib's BeginMode3D loads its own,
 *        with its fixed far plane), nothing otherwise.
 * @param frame The frame (IsDepthReversed, Projection).
 */
void WorldRendererCommon_LoadCameraProjection(const WorldRenderFrame* frame);

/**
 * @brief Ends a camera pass's depth: gives raylib back its depth state (GLDepth_EndReversed) while the frame draws
 *        reversed, nothing otherwise. Call after EndMode3D, before EndTextureMode.
 * @param self The renderer.
 * @param frame The frame (IsDepthReversed).
 */
void WorldRendererCommon_EndCameraDepth(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Creates a colour-only framebuffer, for passes that write every pixel without depth.
 * @param width The target's width in pixels.
 * @param height Its height.
 * @param format Its texture's format: PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 (HDR colour) for most passes,
 *        PIXELFORMAT_UNCOMPRESSED_R32 (one float) for the eye's meter.
 * @returns The target, or a zero target (id 0) if it could not be made complete.
 */
RenderTexture2D WorldRendererCommon_CreateColorTarget(int width, int height, PixelFormat format);

/**
 * @brief Unloads a render target if @p hasTarget says it exists, and clears the flag.
 * @param target The target to unload.
 * @param hasTarget Its existence flag; set to false.
 */
void WorldRendererCommon_UnloadTarget(RenderTexture2D* target, bool* hasTarget);


// WorldRendererScene.c
/**
 * @brief Makes sure the scene target and every pass's resolution-dependent targets match the window size and
 *        the pixelation setting, recreating them all when either changed.
 *
 * A pass whose target cannot be created logs it once and stays off; the scene target falls back from HDR with
 * samplable depth, to HDR with a depth renderbuffer, to 8-bit.
 * @param self The renderer.
 * @param windowWidth The frame target width, in pixels.
 * @param windowHeight The frame target height, in pixels.
 * @returns Success; ErrorCode_InvalidState if not even the 8-bit scene target can be created.
 */
Error WorldRendererScene_EnsureTargets(WorldRenderer* self, int windowWidth, int windowHeight);

/** @brief Unloads the scene target and every pass's resolution-dependent targets. */
void WorldRendererScene_ReleaseTargets(WorldRenderer* self);

/**
 * @brief The scene pass: clears the scene target, draws the sky, then the debug grid, the lit models and sprites
 *        (cutout, decals, then blended and additive back to front) and the light gizmos passing the frame's scene
 *        filter. Opens and closes the scene target.
 */
void WorldRendererScene_Render(WorldRenderer* self, const WorldRenderFrame* frame);


// WorldRendererGeometry.c
/** @brief Whether an object with the given OmitPixelation flag passes @p filter. */
bool WorldRendererGeometry_PassesFilter(bool omitPixelation, WorldRendererPixelFilter filter);

/**
 * @brief Loads the unlit fallback's skinned shader (world_unlit_skinned) and finds its palette. Without it skinned
 *        meshes draw their bind pose while the PBR shader is unavailable.
 */
void WorldRendererGeometry_LoadShaders(WorldRenderer* self);

/** @brief Makes every skinned shader's palette forget the pose it holds (at the start of each frame, before the passes:
 *  WorldRendererPalette). */
void WorldRendererGeometry_ResetPalettes(WorldRenderer* self);

/**
 * @brief Draws one LOD of a cached model mesh by mesh, with the LOD's import transform inside the object's render-space
 *        matrix (WorldRenderModel_GetLodTransform), each mesh with a material built on the stack (the asset's maps copied, the
 *        entry's variant applied to the copy, the pass's shader, the object's tint applied to the copy, times the LOD's
 *        debug colour while the LOD tinting is on), so nothing is written to an asset. The caller uploads the draw's
 *        dither to the pass's shaders first (both, while the entry has a pose), and in the lit pass the object's forward
 *        point lights.
 *
 * SKINNING. While the entry has a pose (WorldRenderModel.Pose) and the LOD's model has a skeleton with its bones,
 * the pose's palette is uploaded into @p shaders' skinned shader (once per pose per frame) and the LOD's skinned meshes
 * (GameSkeleton_IsMeshSkinned) draw with it; its other meshes draw with the rigid shader. Without a pose, a skinned
 * shader or room in its palette, every mesh draws with the rigid shader (skinned ones in their bind pose).
 *
 * Must run inside a 3D mode (BeginMode3D or explicit rlgl matrices). A LOD past the world model's last draws nothing.
 * @param self The renderer.
 * @param model The cached model; must have an asset.
 * @param lod The LOD to draw (the entry's LOD state says which: WorldLod.h).
 * @param shaders The pass's shaders; must not be NULL.
 * @param isLit Whether this is the lit (PBR) pass: then each material's PBR factors are uploaded before its meshes, into
 *        the program the mesh draws with.
 */
void WorldRendererGeometry_DrawModel(WorldRenderer* self, const WorldRenderModel* model, size_t lod,
    const WorldRendererMeshShaders* shaders, bool isLit);

/** @brief Draws the camera's models (the visibility's camera list) that pass @p filter. With the PBR shader (the lit
 *  pass) it uploads each object's forward point lights and draws its LODs (both during a cross-fade) dithered by its fade
 *  and its part of the cross-fade; otherwise (the materials' own shaders, the unlit skinned one for skinned meshes) it
 *  draws the LOD with the larger share, solid. */
void WorldRendererGeometry_DrawModels(WorldRenderer* self, WorldRendererPixelFilter filter);

/**
 * @brief Builds the material one mesh of a model entry's LOD draws with, on the stack: the asset material's maps copied
 *        into @p outMaps, the entry's variant applied to the copy, the LOD's look (the entry's tint, times the LOD's
 *        debug colour and its glow while the LOD tinting is on), the pass's shader; in the lit pass the copy's PBR
 *        factors and map flags uploaded into @p litProgram first. Nothing is written to an asset. (WorldRendererGeometry_DrawModel
 *        draws each mesh with it; the instanced draws too.)
 * @param self The renderer.
 * @param model The entry (its Asset, HasVariant, VariantIndex and Tint are read).
 * @param rayModel The LOD's raylib model.
 * @param meshIndex The mesh.
 * @param lod The LOD (for its look).
 * @param passShader The shader to draw with, or NULL to keep the material's own.
 * @param litProgram In the lit pass the PBR program @p passShader belongs to; NULL otherwise.
 * @param outMaps [out] Receives the maps the material points at; must hold WORLD_RENDERER_MATERIAL_MAP_COUNT and
 *        outlive the draw.
 * @returns The material.
 */
Material WorldRendererGeometry_PrepareMaterial(const WorldRenderer* self, const WorldRenderModel* model, Model rayModel,
    int meshIndex, size_t lod, const Shader* passShader, const WorldRendererPbrProgram* litProgram,
    MaterialMap* outMaps);

/** @brief Per-channel 0..255 colour multiply (a*b/255, truncated), as raylib folds an object's tint into a material's
 *  albedo colour. */
Color WorldRendererGeometry_MultiplyColor(Color a, Color b);

/** @brief Draws a small wireframe sphere at each light (its render-space position), tinted by its colour (a debug aid).
 *  Inside BeginMode3D. */
void WorldRendererGeometry_DrawLightGizmos(WorldRenderer* self);


// WorldRendererCollision.c
/** @brief Draws the collision world's debug shapes (_collisionDebug) as wireframes in their world placements, coloured by
 *  layer, and its contact points (a debug aid), moved into render space for the frame's origin. Inside BeginMode3D;
 *  reads only. */
void WorldRendererCollision_Draw(WorldRenderer* self, const WorldRenderFrame* frame);


// WorldRendererSprites.c
/** @brief Constructs the sprite pass's CPU state (its per-frame lists). Called once by WorldRenderer_Create; pair with
 *  WorldRendererSprites_Deconstruct. */
void WorldRendererSprites_Construct(WorldRenderer* self);

/** @brief Unloads the quad mesh and frees the per-frame lists. Called once by WorldRenderer_Deconstruct, with the GL
 *  context alive. */
void WorldRendererSprites_Deconstruct(WorldRenderer* self);

/**
 * @brief Loads the lit sprite shader (and prepares its lighting uniforms) and builds the unit quad.
 *
 * Without either, sprites are not drawn at all (logged once): every sprite pass draws through the quad, and the
 * G-buffer's and shadows' sprites would otherwise show where no sprite is seen.
 */
void WorldRendererSprites_LoadShaders(WorldRenderer* self);

/** @brief Whether sprites can be drawn: the quad and the lit sprite shader are there. */
bool WorldRendererSprites_IsAvailable(const WorldRenderer* self);

/**
 * @brief Computes each cached sprite's matrices for this frame, in render space (facing the camera at the origin; facing
 *        the cascades' light and the cube shadow's light while those run) and the draw order of the blended and additive
 *        sprites the camera's passes draw, back to front.
 *
 * Needs the frame's View, IsCascadeShadowActive / ShadowDirection, IsPointShadowActive / ShadowLightIndex and the
 * frame's visibility. No allocation once the lists have grown to the sprite count.
 */
void WorldRendererSprites_PrepareFrame(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Returns a cached sprite's matrices for this frame (WorldRendererSprites_PrepareFrame).
 * @param self The renderer.
 * @param index The sprite's index in the render cache's sprite list.
 * @returns The entry (borrowed until the next PrepareFrame), or NULL when this frame has none for it (sprites are
 *          unavailable, or @p index is past the list).
 */
const WorldRendererSpriteFrame* WorldRendererSprites_GetFrame(const WorldRenderer* self, size_t index);

/**
 * @brief Draws the unit quad once with @p shader (its own location table), the sprite's frame texture on the albedo
 *        map, @p tint as colDiffuse and @p transform as the model matrix. Culling should be off (sprites are
 *        two-sided, and a flip mirrors the quad); call inside a 3D mode.
 */
void WorldRendererSprites_DrawQuad(WorldRenderer* self, Shader shader, const WorldRenderSprite* sprite, Color tint,
    Matrix transform);

/**
 * @brief The lit sprite draws of a scene or crisp pass, for the camera's sprites passing @p filter: the cutout sprites
 *        (opaque, writing depth), then the decals (over the opaque world, writing no depth), then the blended and
 *        additive sprites back to front (writing no depth). Cutout sprites dither by their fade; blended and additive
 *        ones take their draw-distance fade into their alpha. Call after the opaque models, with the frame's lighting
 *        uploaded (WorldRendererLighting_UploadFrame), inside BeginMode3D. Restores culling, the depth mask and the blend
 *        mode.
 * @param self The renderer (its sprite matrices prepared for this frame: WorldRendererSprites_PrepareFrame).
 * @param filter Which sprites (by their effective OmitPixelation).
 * @param applyScreenAo Whether the pass's AO map lines up with it (the scene pass; not the window-resolution crisp
 *        pass).
 */
void WorldRendererSprites_DrawLit(WorldRenderer* self, WorldRendererPixelFilter filter, bool applyScreenAo);


// WorldRendererLighting.c
/**
 * @brief Loads the PBR programs, the rigid one and the skinned one: caches each one's uniform locations, builds its
 *        location table and pass shader, points its samplers at their fixed units, checks how many point lights it
 *        supports, and finds the skinned one's palette.
 *
 * Without the rigid one models draw unlit with their materials' own shaders; without the skinned one skinned meshes draw
 * with the rigid one, in their bind pose.
 */
void WorldRendererLighting_LoadShader(WorldRenderer* self);

/**
 * @brief Prepares a shader that includes shaders/common/lighting.glsl: caches its frame-lighting uniform locations,
 *        uploads the constants (the shadow bias and texel size), points its shadow and AO samplers at their fixed units,
 *        and checks how many point lights its arrays hold (logging once when fewer than WORLD_MAX_FORWARD_LIGHTS).
 * @param self The renderer.
 * @param shader The shader (its program).
 * @param assetName Its asset name, for the log line.
 * @param outLocations [out] Receives the locations.
 */
void WorldRendererLighting_PrepareLitShader(WorldRenderer* self, Shader shader, const unsigned char* assetName,
    WorldRendererLitLocations* outLocations);

/**
 * @brief Computes the frame's sky light: the C mirror's tables built again when the air changed, the sky's light at
 *        the camera (frame->SkyLight: WorldSkyLight_GetFrameLight), the light the air is lit by and the sun's disc, the
 *        shadow light chosen from the light that reaches the camera (frame->ShadowLight, ShadowDirection, ShadowFade:
 *        WorldEnvironment_ChooseShadowLight), whether the lit shaders read it by height, the scene luminance eye
 *        adaptation starts from and the night vision's calibration.
 *
 * Needs WorldRendererSky_PrepareFrame (the moonlit sky) and WorldRendererAtmosphere_PrepareFrame (the air, the view
 * height, light pollution, whether the tables and the volume run) first; the shadow decisions need it.
 */
void WorldRendererLighting_PrepareSkyLight(WorldRenderer* self, WorldRenderFrame* frame);

/**
 * @brief Computes the frame's shadow strength into @p frame and builds the forward-light candidates (every light except
 *        the frame's cube-shadow light). Needs IsPointShadowActive and ShadowLightIndex.
 */
void WorldRendererLighting_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame);

/**
 * @brief Uploads the frame's lighting uniforms to every lit shader the renderer has (the two PBR programs and the sprite
 *        shader), with the pass's dither pixel scale, and binds the shadow maps, the cube shadow and (when
 *        @p applyScreenAo) the AO map to their fixed units.
 *
 * Call before the lit models and sprites of a pass. The crisp overlay passes false for @p applyScreenAo: it renders at
 * window resolution, where the low-res AO map would not line up; for the same reason its @p ditherPixelScale is window /
 * scene (the scene pass's is 1), which keeps the dither on the scene's pixel grid.
 */
void WorldRendererLighting_UploadFrame(WorldRenderer* self, const WorldRenderFrame* frame, bool applyScreenAo,
    Vector2 ditherPixelScale);

/**
 * @brief Reach-culls the frame's forward lights against a bounding sphere in render space and uploads the strongest few
 *        to a lit shader, for the object drawn next.
 * @param self The renderer.
 * @param shader The lit shader (its program).
 * @param locations Its locations (WorldRendererLighting_PrepareLitShader).
 * @param center The object's bounding sphere's centre, in render space.
 * @param radius Its radius.
 */
void WorldRendererLighting_UploadObjectLights(WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, Vector3 center, float radius);

/**
 * @brief Sets the haze multiplier of the lit draw that follows: its placement's resolved haze (1 the real air's, 0
 *        none). Every lit draw sets it, as the uniform keeps the last draw's value.
 * Interior lighting, not built yet, will multiply @p haze here by its ambient zones' haze multiplier.
 * @param shader The lit shader (its program).
 * @param locations Its locations (WorldRendererLighting_PrepareLitShader).
 * @param haze The multiplier (>= 0; a negative or NaN one counts as 0 in the shader).
 */
void WorldRendererLighting_SetDrawHaze(Shader shader, const WorldRendererLitLocations* locations, float haze);

/**
 * @brief Uploads one material's PBR scalar factors and map-presence flags to a PBR program, for its meshes drawn next.
 *
 * DrawMesh uploads colDiffuse and binds the maps itself, but not these. Pass the per-draw copy, after the variant was
 * applied to it, so its factors and maps are the ones drawn.
 * @param program The program the meshes draw with (the rigid one or the skinned one).
 * @param material The material.
 */
void WorldRendererLighting_UploadMaterial(const WorldRendererPbrProgram* program, const Material* material);

/** @brief Logs a line about the frame's light at the camera (with the status, WorldRenderer_LogStatus): the sun's and the
 *  moon's through the air, the sky's irradiance, the ground's bounce, the night's light, the scene luminance and the
 *  exposure. */
void WorldRendererLighting_LogStatus(const WorldRenderer* self);


// WorldRendererShadows.c
/** @brief Loads the depth and cube-depth shaders (and their alpha-tested sprite variants) and creates the cascade maps
 *  and the shadow cube. */
void WorldRendererShadows_LoadShaders(WorldRenderer* self);

/** @brief Unloads the cascade maps and the shadow cube. Needs a live GL context. */
void WorldRendererShadows_Unload(WorldRenderer* self);

/**
 * @brief Picks the light that casts the cube shadow: the CastsShadows light with positive intensity and reach
 *        nearest the camera (the origin of render space: the shortest RenderPosition).
 * @returns Its index in the render cache's lights, or SIZE_MAX when no light qualifies.
 */
size_t WorldRendererShadows_SelectPointLight(const WorldRenderer* self);

/**
 * @brief Fills the frame's shadow values the passes and the visibility share, in render space: both cascades' light
 *        matrices and boxes (while IsCascadeShadowActive; from frame->ShadowDirection, around the camera, each centre
 *        moved from its last one by whole texels and the new one kept: WorldRenderSpace_StepCascade) and the cube
 *        shadow's light position and far range (while IsPointShadowActive). Call after the shadow decisions, before the
 *        visibility; once per frame (it moves the cascades' grids).
 */
void WorldRendererShadows_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame);

/** @brief Renders both cascades (the far one only when its map exists) from the frame's shadow light, with the frame's
 *  light matrices. Each draws its visibility list: models cast per their shadow tier, cutout sprites into both, turned
 *  toward the light (WorldRendererSprites_PrepareFrame's CascadeTransform); each caster dithered by its fade. */
void WorldRendererShadows_RenderCascades(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Renders the frame's shadow light into the six cube faces, drawing its visibility list: the models and cutout
 *        sprites (turned toward the light) within its reach, each dithered by its fade.
 */
void WorldRendererShadows_RenderPointLight(WorldRenderer* self, const WorldRenderFrame* frame);


// WorldRendererGBuffer.c
/** @brief Loads the G-buffer shaders (models, cutout sprites) and the AO shader. */
void WorldRendererGBuffer_LoadShaders(WorldRenderer* self);

/** @brief Creates the G-buffer and AO targets for a scene of @p width x @p height (call with the scene target in
 *  place). A failure is logged once and leaves the pass off. */
void WorldRendererGBuffer_CreateTargets(WorldRenderer* self, int width, int height);

/** @brief Unloads the G-buffer and AO targets. */
void WorldRendererGBuffer_ReleaseTargets(WorldRenderer* self);

/** @brief Draws the camera's models and cutout sprites (not decals) passing the scene filter into the G-buffer (view
 *  normals + flag): solid while their fade is below WORLD_VISIBILITY_GBUFFER_FADE_LIMIT, not at all from there on (the
 *  G-buffer never dithers, so outlines and AO switch off once, at mid-fade). */
void WorldRendererGBuffer_RenderNormals(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Runs the AO pre-pass from the G-buffer into the AO target, at frame->AoStrength. */
void WorldRendererGBuffer_RenderAo(WorldRenderer* self, const WorldRenderFrame* frame);


// WorldRendererAtmosphere.c
/** @brief Constructs the atmosphere pass's state (the tables switched on, none built, the volume's view off) and
 *  allocates the C mirror's two table sets (built on the first frame that needs them). Called once by
 *  WorldRenderer_Create. */
void WorldRendererAtmosphere_Construct(WorldRenderer* self);

/**
 * @brief Loads the four table builders (the transmittance, the multiple scattering, the sky view, the irradiance) and
 *        the volume's, and creates their targets (fixed sizes, made once). Without any of the tables' the tables are
 *        unavailable: the sky marches per pixel, nothing is hazed and the lit shaders take the sky's light at the
 *        camera (logged once); without the volume's, nothing is hazed and the same (logged once).
 */
void WorldRendererAtmosphere_LoadShaders(WorldRenderer* self);

/** @brief Unloads the tables' and the volume's targets. Called once by WorldRenderer_Deconstruct, with the GL context
 *  alive. */
void WorldRendererAtmosphere_Unload(WorldRenderer* self);

/** @brief Frees the C mirror's tables (CPU memory). Called once by WorldRenderer_Deconstruct. */
void WorldRendererAtmosphere_Deconstruct(WorldRenderer* self);

/**
 * @brief Keeps the C mirror's tables on the air (stale as the GPU's irradiance table is:
 *        WorldAtmosphere_IsIrradianceStale; WorldSkyLight_UpdateBuilder): built whole the first time; when the air
 *        changes, rebuilt into the second set over frames and swapped in when done, about
 *        SKY_LIGHT_BUILD_BUDGET_NANOSECONDS of steps a frame (WorldSkyLight_RunBuildStep: a step runs only while the
 *        time so far and the last step's fit in the budget, the first always). Called every frame by
 *        WorldRendererLighting_PrepareSkyLight.
 */
void WorldRendererAtmosphere_UpdateSkyLightTables(WorldRenderer* self, const WorldAtmosphereMedium* medium);

/** @brief Whether the tables can be built: their four shaders and targets exist. */
bool WorldRendererAtmosphere_IsAvailable(const WorldRenderer* self);

/** @brief Whether the volume can be built: the tables can, and its shader and target exist. */
bool WorldRendererAtmosphere_IsVolumeAvailable(const WorldRenderer* self);

/**
 * @brief Fills the frame's atmosphere: the air's coefficients from the environment, the haze (the environment's and the
 *        player's fog strength), light pollution's light in the air, the height the sky is seen from (the origin's Y
 *        above the atmosphere's ground, clamped), whether the tables draw the sky this frame (available and switched on)
 *        and whether the volume hazes it (the tables active, the volume available, the haze's scale above 0). Needs the
 *        frame's Environment, Origin and Daylight.
 */
void WorldRendererAtmosphere_PrepareFrame(const WorldRenderer* self, WorldRenderFrame* frame);

/**
 * @brief The atmosphere pass, first in the frame while frame->AreAtmosphereTablesActive: the transmittance and multiple
 *        scattering tables built again when the air differs from the one they hold, the irradiance table after them (or
 *        when the haze's forward scattering or the share of multiple scattering changed), then the sky view: the sun's
 *        half (with light pollution's light and the far ground's ambient) and, while the moonlit sky shows
 *        (frame->MoonSkyIntensity above 0), the moon's; then the volume (all of it while frame->IsHazeActive, only its
 *        last row while only frame->IsSkyLightPerHeight). Leaves the transmittance and sky-view tables bound on their
 *        units. Needs the frame's sky light (WorldRendererLighting_PrepareSkyLight) and WorldRendererSky_PrepareFrame
 *        first.
 */
void WorldRendererAtmosphere_Render(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Caches a shader's haze uniform locations (common/haze). Call once at load.
 * @param shader The shader (its program).
 * @param isVolumeOnFixedUnit true for a lit program, whose volume sampler is pointed at WorldRendererTextureUnit_HazeVolume
 *        here (a 2D pass binds the volume itself: WorldRendererAtmosphere_BindVolumeTexture).
 * @param outLocations [out] Receives the locations.
 */
void WorldRendererAtmosphere_PrepareHazeShader(Shader shader, bool isVolumeOnFixedUnit,
    WorldRendererHazeLocations* outLocations);

/**
 * @brief Uploads the frame's haze to a shader (common/haze): whether the volume holds it, the distance scale, the start,
 *        and the ground haze (its base relative to the camera).
 */
void WorldRendererAtmosphere_UploadHaze(Shader shader, const WorldRendererHazeLocations* locations,
    const WorldRenderFrame* frame);

/** @brief Binds the volume on WorldRendererTextureUnit_HazeVolume while it holds this frame's haze or sky light, leaving
 *  unit 0 the active one: for the lit passes (DrawMesh draws), before their draws. */
void WorldRendererAtmosphere_BindVolume(const WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Binds the volume to a 2D pass's volume sampler (SetShaderValueTexture, after BeginShaderMode) while the frame's
 *  haze is active. */
void WorldRendererAtmosphere_BindVolumeTexture(const WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererHazeLocations* locations);

/** @brief Draws the volume's debug view over the scene target (WorldRenderer_SetHazeView): one slice's in-scatter or
 *  transmittance, stretched over the screen as the readers sample it. Nothing while the view is off or the haze
 *  inactive. After the scene pass. */
void WorldRendererAtmosphere_DrawView(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Caches a shader's atmosphere uniform locations (common/atmosphere) and points its transmittance and sky-view
 *        samplers at their fixed units. Call once at load.
 * @param shader The shader (its program).
 * @param outLocations [out] Receives the locations.
 */
void WorldRendererAtmosphere_PrepareShader(Shader shader, WorldRendererAtmosphereLocations* outLocations);

/**
 * @brief Uploads the frame's atmosphere to a shader: the air, the height the sky is seen from and whether the tables hold
 *        the sky (the shader reads them, or marches).
 */
void WorldRendererAtmosphere_UploadFrame(Shader shader, const WorldRendererAtmosphereLocations* locations,
    const WorldRenderFrame* frame);

/** @brief Binds the transmittance and sky-view tables on their fixed units (WorldRendererTextureUnit_Atmosphere*) while
 *  the frame's tables are active, leaving unit 0 the active one. For every pass that reads them, before its draw. */
void WorldRendererAtmosphere_BindTables(const WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Logs two lines about the atmosphere (with the status, WorldRenderer_LogStatus): the tables (on, off or
 *  unavailable, their sizes, how often they were built, the height the sky is seen from, the last render's CPU time)
 *  and the haze (the volume, the frame's haze, the ground haze, the debug view). */
void WorldRendererAtmosphere_LogStatus(const WorldRenderer* self);


// WorldRendererSky.c
/** @brief Constructs the sky pass's CPU state (the sky texture slots' name buffers). Called once by
 *  WorldRenderer_Create; pair with WorldRendererSky_Deconstruct. */
void WorldRendererSky_Construct(WorldRenderer* self);

/** @brief Releases the sky textures (retires their asset users) and frees the slots' name buffers. Called once by
 *  WorldRenderer_Deconstruct, before the shader user is retired. */
void WorldRendererSky_Deconstruct(WorldRenderer* self);

/** @brief Loads the atmospheric sky shader (optional: the scene then clears to a CPU gradient colour). */
void WorldRendererSky_LoadShader(WorldRenderer* self);

/**
 * @brief Loads the sky textures the environment names (MilkyWayTexture, MoonTexture) when they differ from the loaded
 *        ones, each under its own asset user. Cheap when nothing changed; needs a live GL context when something did.
 *        A name that fails is logged once and not retried until the environment names another: without the Milky Way
 *        texture the sky draws no Milky Way, without the moon's the disc is a flat grey.
 */
void WorldRendererSky_SyncTextures(WorldRenderer* self, const WorldEnvironment* environment);

/**
 * @brief Computes the frame's moon (its light on the atmosphere and its disc) and the skyglow.
 *
 * Needs frame->Environment, the sun's and the moon's directions, Daylight and MoonIllumination.
 */
void WorldRendererSky_PrepareFrame(WorldRenderFrame* frame);

/**
 * @brief Computes the frame's night sky: the limiting magnitude, the naked eye's threshold against the sky behind
 *        the stars (the twilight's light from the sun on the real scale, the moonlit sky and light pollution's:
 *        NightSky_GetLimitingMagnitude), and the Milky Way's radiance by it.
 *
 * Needs WorldRendererSky_PrepareFrame and the frame's sky light (WorldRendererLighting_PrepareSkyLight).
 */
void WorldRendererSky_PrepareNightSky(WorldRenderFrame* frame);

/**
 * @brief Whether the moon's disc shows this frame: the moon is on, above the horizon and far enough from the sun
 *        (frame->MoonDisc.Opacity > 0). Needs WorldRendererSky_PrepareFrame first.
 */
bool WorldRendererSky_IsMoonVisible(const WorldRenderFrame* frame);

/**
 * @brief Draws the atmospheric sky (with the moonlit sky, the skyglow, the moon's disc when frame->IsMoonActive, and the
 *        Milky Way when frame->IsMilkyWayActive) as a full-screen pass into the bound scene target. Must run after the
 *        clear and BEFORE the 3D geometry: it writes no depth, so the geometry draws over it.
 */
void WorldRendererSky_Draw(WorldRenderer* self, const WorldRenderFrame* frame);


// WorldRendererStars.c
/** @brief Loads the star shader (optional: no stars without it). */
void WorldRendererStars_LoadShader(WorldRenderer* self);

/** @brief Unloads the star mesh. Needs a live GL context. */
void WorldRendererStars_Unload(WorldRenderer* self);

/**
 * @brief Rebuilds the star catalogue and its mesh when the environment's StarSeed or star count (from StarDensity)
 *        differs from the current one. Cheap when nothing changed; needs a live GL context when something did.
 *        A catalogue that cannot be built is logged once and leaves no stars.
 */
void WorldRendererStars_SyncCatalogue(WorldRenderer* self, const WorldEnvironment* environment);

/**
 * @brief Draws the star field into the bound scene target, right after the sky and before the geometry: additive,
 *        depth-tested, writing no depth (the post passes treat far depth as sky), and none behind the moon's disc.
 *        Only while frame->AreStarsActive.
 */
void WorldRendererStars_Draw(WorldRenderer* self, const WorldRenderFrame* frame);


// WorldRendererExposure.c
/** @brief Makes the eye, loads the meter's shader and makes its target and readback (once, with the other shaders); a
 *  missing part is logged once and the eye adapts to the anchor (the sky's light at the camera) instead. */
void WorldRendererExposure_Load(WorldRenderer* self);

/** @brief Frees the meter's target and the readback's buffers (and any copies in flight). */
void WorldRendererExposure_Unload(WorldRenderer* self);

/** @brief Whether the eye meters the picture (WorldRendererExposure_Load made everything it needs). */
bool WorldRendererExposure_IsAvailable(const WorldRenderer* self);

/**
 * @brief Returns the exposure the eye is at before this frame's update (the sun's disc and the sun shafts are drawn
 *        against it): its level's (the anchor's before it started) under this frame's sky light.
 * @param self The renderer.
 * @param frame The frame (BaseSceneLuminance and SkyLight must be computed).
 * @returns The exposure.
 */
float WorldRendererExposure_GetCurrent(const WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Moves the eye on by a frame and returns the tonemap's exposure: takes the newest blocks the GPU has finished,
 *        measures them, eases the eye, then meters @p source for the frames to come and asks for its copy. Sets the
 *        frame's AdaptedLuminance, AdaptedRealLuminance and IsExposureMetered.
 * @param self The renderer.
 * @param frame The frame (BaseSceneLuminance and SkyLight must be computed).
 * @param source The finished HDR scene, as the tonemap reads it (the scene target or the outlines').
 * @param deltaSeconds Real seconds since the previous render; 0, negative or NaN holds the eye.
 * @returns The exposure (finite, within EYE_ADAPTATION_MIN_EXPOSURE and EYE_ADAPTATION_MAX_EXPOSURE).
 */
float WorldRendererExposure_Update(WorldRenderer* self, WorldRenderFrame* frame, RenderTexture2D source,
    float deltaSeconds);


// WorldRendererPost.c
/** @brief Loads the tonemap, bloom, sun-shaft and outline shaders. */
void WorldRendererPost_LoadShaders(WorldRenderer* self);

/** @brief Creates the outline, bloom and sun-shaft targets for a scene of @p width x @p height. A failure is
 *  logged once and leaves that pass off. */
void WorldRendererPost_CreateTargets(WorldRenderer* self, int width, int height);

/** @brief Unloads the outline, bloom and sun-shaft targets. */
void WorldRendererPost_ReleaseTargets(WorldRenderer* self);

/** @brief Runs the outline pass from the scene colour and the G-buffer (its normals, flags and depth; the scene depth
 *  when the G-buffer's is not samplable) into the outline target. */
void WorldRendererPost_RenderOutlines(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Bright-passes @p source into the bloom buffers and blurs it, leaving the bloom in the A buffer (the sky masked
 *  out by the scene's depth, read in the frame's convention). */
void WorldRendererPost_RenderBloom(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D source);

/**
 * @brief Projects the sun to screen UV for the sun shafts.
 * @param frame The frame (camera matrices, sun direction).
 * @param outUv [out] The sun's screen position in [0,1] UV (may lie a little outside the screen).
 * @param outFade [out] A 0..1 weight fading the shafts out near the screen margin and the horizon.
 * @returns false when the sun is too low, behind the camera or too far off-screen for shafts.
 */
bool WorldRendererPost_ComputeSunScreenUv(const WorldRenderFrame* frame, Vector2* outUv, float* outFade);

/** @brief Radially blurs the unoccluded sky of @p source from frame->SunScreenUv into the sun-shaft buffer. */
void WorldRendererPost_RenderSunshafts(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D source);

/**
 * @brief Blits @p source into @p target through the tonemap (exposure, bloom and sun shafts added in HDR),
 *        point-upscaling and flipping it as render textures need.
 */
void WorldRendererPost_Tonemap(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D source,
    RenderTexture2D target);


// WorldRendererTerrain.c
/** @brief Constructs the terrain's CPU state (its list of GPU meshes). Called once by WorldRenderer_Create; pair with
 *  WorldRendererTerrain_Deconstruct. */
void WorldRendererTerrain_Construct(WorldRenderer* self);

/** @brief Unloads every GPU mesh and texture array and frees the list. Called once by WorldRenderer_Deconstruct, with the
 *  GL context alive, before the model user is retired. */
void WorldRendererTerrain_Deconstruct(WorldRenderer* self);

/** @brief Loads the lit terrain shader (world_terrain) and prepares its lighting, dither and chunk key uniforms and
 *  its materials' and rules' uniforms, the arrays' samplers on their units, and the texture arrays' GL functions
 *  (unavailable: logged once). Optional: without it the chunks draw unlit with raylib's default shader. */
void WorldRendererTerrain_LoadShader(WorldRenderer* self);

/** @brief Unloads every GPU mesh and texture array: before the model user the terrains are held under is replaced (an
 *  address may hold another terrain then). Needs the GL context. */
void WorldRendererTerrain_ReleaseMeshes(WorldRenderer* self);

/**
 * @brief Brings the GPU meshes up to date with the render cache, synced for this frame: a terrain seen for the first time
 *        gets its material uniforms and its texture arrays (made once; a failure is logged once and the terrain
 *        shows its materials' mean colours), a terrain whose chunk layout changed gets new meshes, a chunk whose revision
 *        changed is uploaded again (only the listed chunks: those of shown terrains), and the meshes and arrays of
 *        terrains no cache entry resolves any more are unloaded. Cheap when nothing changed. Needs the GL context. Render
 *        reads: the terrains (meshes and materials) are only read.
 */
void WorldRendererTerrain_PrepareFrame(WorldRenderer* self);

/**
 * @brief The scene pass's terrain: the camera's chunks (the visibility's list) through the lit terrain shader, each
 *        terrain's arrays bound and its materials' and rules' uniforms uploaded as the pass turns to its chunks, each
 *        chunk with its wrapped offsets and height, its forward lights (by its sphere), its dither (its fade) and, while
 *        the chunk key is on, its cell's colour; then the key's wireframe. Without the shader they draw unlit with
 *        raylib's default shader, solid. Call after WorldRendererLighting_UploadFrame, inside BeginMode3D, before the
 *        sprites (decals lie on the terrain).
 */
void WorldRendererTerrain_DrawLit(WorldRenderer* self);

/**
 * @brief Draws the camera's chunks solid enough for the G-buffer (fade below WORLD_VISIBILITY_GBUFFER_FADE_LIMIT) with
 *        @p shader (world_normal), each with its outline flag.
 * @param self The renderer.
 * @param shader The G-buffer shader (its program).
 * @param locOutlineFlag Its outline flag's location.
 * @param locHazeMultiplier Its haze multiplier's location (an outlined chunk's flag holds its haze).
 */
void WorldRendererTerrain_DrawNormals(WorldRenderer* self, Shader shader, int locOutlineFlag, int locHazeMultiplier);

/**
 * @brief Draws a shadow pass's chunk casters (its visibility list) with @p shader (world_depth or world_cube_depth), each
 *        dithered by its fade. Inside the pass's matrices and culling.
 * @param self The renderer.
 * @param shader The depth shader (its program).
 * @param dither Its dither locations.
 * @param pass The shadow pass.
 */
void WorldRendererTerrain_DrawCasters(WorldRenderer* self, Shader shader, const WorldRendererDitherLocations* dither,
    WorldVisibilityPass pass);

/**
 * @brief The status's "terrain materials" feature: unavailable without the lit terrain shader or texture arrays,
 *        active while a terrain the camera's pass drew had its arrays, inactive otherwise.
 */
WorldRendererFeatureState WorldRendererTerrain_GetMaterialsState(const WorldRenderer* self);

/** @brief Logs one line per terrain the renderer keeps: its materials and its texture arrays' sizes (with the
 *  status, WorldRenderer_LogStatus). */
void WorldRendererTerrain_LogMaterials(const WorldRenderer* self);


// WorldRendererInstances.c
/** @brief Constructs the instancing pass's CPU state (its buffer lists; instancing on). Called once by
 *  WorldRenderer_Create; pair with WorldRendererInstances_Deconstruct. */
void WorldRendererInstances_Construct(WorldRenderer* self);

/** @brief Unloads every records buffer and frees the lists. Called once by WorldRenderer_Deconstruct, with the GL
 *  context alive. */
void WorldRendererInstances_Deconstruct(WorldRenderer* self);

/** @brief Loads the G-buffer's and the shadows' instanced twins (world_normal_instanced, world_depth_instanced,
 *  world_cube_depth_instanced) and prepares their uniforms and the lit twin's (_pbr._instanced, which
 *  WorldRendererLighting_LoadShader loads: call after it). Each is optional: without it that pass draws copies one at a
 *  time. */
void WorldRendererInstances_LoadShaders(WorldRenderer* self);

/** @brief Unloads every records buffer (when the world changes: WorldRenderer_PrepareWorld). Needs the GL context. */
void WorldRendererInstances_ReleaseBuffers(WorldRenderer* self);

/**
 * @brief Brings the records' GPU buffers up to date with the render cache, synced for this frame: each entry's build of
 *        cells uploaded once (a new build replaces the old buffer), the buffers no entry holds any more unloaded. Cheap
 *        when nothing changed. Needs the GL context. A buffer raylib could not make (0: no GL context) is logged once,
 *        and that entry then draws its copies one at a time; a GL_OUT_OF_MEMORY from the upload is not caught (raylib
 *        checks no upload's GL errors).
 */
void WorldRendererInstances_PrepareFrame(WorldRenderer* self);

/**
 * @brief The lit pass's copies: the camera's cells (none in the crisp overlay: copies are never crisp), each with its
 *        forward lights (by its sphere) and the LODs its copies can show, instanced through world_pbr_instanced (each
 *        copy's fade and LOD band in the vertex stage), or one at a time through world_pbr while instancing is off or the
 *        twin or a buffer is missing; without PBR, one at a time with the materials' own shaders, solid. Call after
 *        WorldRendererLighting_UploadFrame, inside BeginMode3D.
 * @param self The renderer.
 * @param frame The frame (its zoom factor).
 * @param filter The pass's pixel filter (CrispOnly draws nothing).
 */
void WorldRendererInstances_DrawLit(WorldRenderer* self, const WorldRenderFrame* frame, WorldRendererPixelFilter filter);

/**
 * @brief The G-buffer's copies: the camera's cells solid enough for it, each copy solid as the LOD with the larger
 *        share until mid-fade, with its component's outline flag; instanced through world_normal_instanced, or one at a
 *        time through @p shader (world_normal).
 * @param self The renderer.
 * @param frame The frame.
 * @param shader The G-buffer's model shader (its program).
 * @param locOutlineFlag Its outline flag's location.
 * @param locHazeMultiplier Its haze multiplier's location (an outlined copy's flag holds its component's haze).
 */
void WorldRendererInstances_DrawNormals(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    int locOutlineFlag, int locHazeMultiplier);

/**
 * @brief A shadow pass's copies (its visibility list of cells): each copy as the LOD with the larger share, dithered by
 *        its own fade; instanced through the pass's twin (world_depth_instanced, or world_cube_depth_instanced with the
 *        frame's cube light), or one at a time through @p shader. Inside the pass's matrices and culling.
 * @param self The renderer.
 * @param frame The frame (the zoom factor; the cube's light for PointShadow).
 * @param shader The pass's ordinary depth shader (its program).
 * @param dither Its dither locations.
 * @param pass The shadow pass.
 */
void WorldRendererInstances_DrawCasters(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererDitherLocations* dither, WorldVisibilityPass pass);

/** @brief The status's "instancing" feature: unavailable without the lit twin (or without PBR lighting), active
 *  while the camera's pass drew a cell instanced, inactive otherwise (switched off, or no copies in view). */
WorldRendererFeatureState WorldRendererInstances_GetState(const WorldRenderer* self);

/** @brief Logs one line about the copies (with the status, WorldRenderer_LogStatus): the components, their copies, the
 *  drawn cells, and the copies the camera's cells show by LOD (the last render's, through the C mirror). */
void WorldRendererInstances_LogStatus(const WorldRenderer* self);


// WorldRendererCrisp.c
/** @brief Loads the crisp-composite shader (optional: flagged objects then render pixelated). */
void WorldRendererCrisp_LoadShader(WorldRenderer* self);

/** @brief Creates the window-resolution overlay target. A failure is logged once and leaves the overlay off. */
void WorldRendererCrisp_CreateTargets(WorldRenderer* self, int windowWidth, int windowHeight);

/** @brief Unloads the overlay target. */
void WorldRendererCrisp_ReleaseTargets(WorldRenderer* self);

/** @brief Draws the OmitPixelation models and cutout sprites, lit, into the window-resolution overlay target. */
void WorldRendererCrisp_Render(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Composites the overlay over @p target (which already holds the tonemapped pixelated frame),
 *        tonemapped at frame->Exposure and depth-tested against the scene so occlusion stays correct.
 */
void WorldRendererCrisp_Composite(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D target);
