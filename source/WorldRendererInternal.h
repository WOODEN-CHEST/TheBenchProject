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
#include "GLSliceMap.h"
#include "GLShadowArray.h"
#include "GLTexture3D.h"
#include "Config.h"
#include "EyeAdaptation.h"
#include "NightVision.h"
#include "WorldAtmosphere.h"
#include "WorldInterior.h"
#include "WorldSkyLight.h"
#include "WorldWeatherLight.h"
#include "WorldClouds.h"
#include "WorldRain.h"
#include "WorldReflections.h"
#include "WorldWater.h"
#include "SpriteTransform.h"
#include "WorldRenderSpace.h"
#include "WorldShadowLevels.h"
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
 *   WorldRendererShadows.c   the shadow levels of the sun or the moon, and the point-light cube shadow
 *   WorldRendererGBuffer.c   the normal/flag G-buffer and the ambient-occlusion pre-pass
 *   WorldRendererAtmosphere.c the atmosphere's tables: transmittance, multiple scattering, the sky view, built
 *                            first in the frame and read by the sky and the stars; the haze's volume; the sky's
 *                            irradiance table and the sky's light by height in the volume's last row
 *   WorldRendererSky.c       the atmospheric sky with the moon, the Milky Way, light pollution's skyglow and the
 *                            drawn clouds over it
 *   WorldRendererClouds.c    the clouds (WorldClouds.h, CloudNoise.h): their noise (3D textures through GLTexture3D.h,
 *                            direct GL calls), their shadow map, their march over the sky's pixels and its
 *                            accumulation, which the sky pass and the stars read; the lit shaders read the shadow map
 *   WorldRendererStars.c     the star field (the seeded catalogue as one-pixel quads)
 *   WorldRendererPost.c      outlines, bloom, sun shafts and the tonemap
 *   WorldRendererExposure.c  the eye's adaptation to the picture: the meter pass, its readback (GLReadback.h,
 *                            direct GL calls) and the exposure (EyeAdaptation.h)
 *   WorldRendererSkyVisibility.c the sky's occupancy volume: what keeps the sky out around the camera, drawn from
 *                            above into height slices (GLSliceMap.h, direct GL calls) with an OR pyramid, which the
 *                            ambient pre-pass and the lit shaders read (interior lighting)
 *   WorldRendererInterior.c  interior lighting's other parts (WorldInterior.h): the ambient zones each lit draw takes,
 *                            the camera's interior for the eye, the lit shaders' interior uniforms, the zones' debug
 *                            wireframes
 *   WorldRendererRain.c      the rain (WorldRain.h): the rain map (what keeps the rain out, a one-layer shadow array
 *                            through GLShadowArray.h), the lit shaders' rain uniforms (the map, the puddles, the sky
 *                            mirrored in water, the bolt's light), the streaks, the splashes and the lightning's bolt
 *   WorldRendererReflections.c the screen-space reflections (WorldReflections.h): the scene target's reflection
 *                            attachments the lit surfaces hand their data to, the switches around the opaque world's
 *                            draws, and the pass that marches every mirroring pixel's ray and adds what it mirrors
 *   WorldRendererWater.c     the water (WorldWater.h): the world models' water slots drawn over the opaque world with
 *                            their water materials, from copies of the scene's colour and depth (what lies under the
 *                            water), handing their reflections over; every other pass but the G-buffer leaves them out
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
/** The most shadow levels the lit shaders read (common/lighting's SHADOW_MAX_LEVELS; the levels' own and the config's
 *  limits are the same: checked below). */
#define WORLD_RENDERER_MAX_SHADOW_LEVELS WORLD_SHADOW_MAX_LEVELS
/** The widest the cloud target is, in texels (a window-sized scene without pixelation is marched at a fraction of it). */
#define WORLD_RENDERER_CLOUD_MAX_WIDTH 640
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
    // The sky's occupancy volume in the lit programs, on the unit the transmittance table has in the sky pass and the
    // stars: the lit programs never read that table (its sampler is declared there and unused), and every lit pass binds
    // the volume with its frame textures after the sky and the stars have drawn.
    WorldRendererTextureUnit_SkyVisibility = 7,           // usampler2D skyVolume
    // The terrain program's material arrays: sampler2DArray, below raylib's material map units' top, because the
    // terrain program draws with no material maps (DrawMesh binds none for it). Other programs' material maps bind
    // GL_TEXTURE_2D (or cube) textures on these units; an array sampler reads only the units' GL_TEXTURE_2D_ARRAY
    // target, which nothing else uses. The units' budget, and what features not built yet will need, is in
    // agents/WorldRuntime.md ("Renderer structure").
    WorldRendererTextureUnit_TerrainAlbedoHeight = 9,   // sampler2DArray materialAlbedoHeight
    WorldRendererTextureUnit_TerrainNormal = 10,        // sampler2DArray materialNormal
    WorldRendererTextureUnit_TerrainRoughnessAo = 11,   // sampler2DArray materialRoughnessAo
    // Every lit program's frame textures.
    WorldRendererTextureUnit_ShadowLevels = 12,     // sampler2DArrayShadow shadowLevels (the sun's or the moon's levels)
    WorldRendererTextureUnit_CloudShadow = 13,      // sampler2D cloudShadowMap (the clouds' shadow)
    WorldRendererTextureUnit_PointShadowCube = 14,  // samplerCube pointShadowCube
    WorldRendererTextureUnit_AmbientOcclusion = 15, // sampler2D ambientOcclusionMap
    // The clouds in the sky pass and the stars (sampler2D cloudLayer: what the clouds add and their transmittance), on a
    // unit those programs leave free; bound manually like the tables.
    WorldRendererTextureUnit_CloudLayer = 9,
    // The cloud pass's own programs (the march and the shadow map: 2D batch draws, which bind only units 0 to 4): the
    // noise volumes on the GL_TEXTURE_3D target, the weather map, the G-buffer's depth and the irradiance table; the
    // transmittance table on its readers' unit 7 and the haze's volume on 8.
    WorldRendererTextureUnit_CloudShape = 9,        // sampler3D cloudShapeNoise
    WorldRendererTextureUnit_CloudDetail = 10,      // sampler3D cloudDetailNoise
    WorldRendererTextureUnit_CloudWeather = 11,     // sampler2D cloudWeatherMap
    WorldRendererTextureUnit_CloudSceneDepth = 12,  // sampler2D sceneDepth
    WorldRendererTextureUnit_CloudIrradiance = 13,  // sampler2D atmosphereIrradianceTable
    // The rain in every lit program and the rain's own programs, on units past raylib's, where no material map and no 2D
    // batch binds: GL 3.3 guarantees 48 units in all (a stage's limit of 16 counts the samplers it reads, not their units'
    // numbers). Bound with the lit passes' frame textures.
    WorldRendererTextureUnit_RainMap = 16,      // sampler2DArrayShadow rainMap (the rain map: a one-layer shadow array)
    WorldRendererTextureUnit_PuddleMask = 17,   // sampler2D puddleMask
    WorldRendererTextureUnit_RainSkyView = 18,  // sampler2D atmosphereSkyViewTable in the lit programs (what water mirrors)
    // A draw's screen light in every lit program (the picture of the screen that lights it, read at a coarse mipmap level
    // for its mean colour), bound per draw with the light (WorldRendererSurfaces_UploadScreenLight); a CRT's mask tile in
    // the CRT programs, bound per CRT draw. Past the rain's units, where nothing else binds.
    WorldRendererTextureUnit_ScreenLight = 19,  // sampler2D screenLightPicture
    WorldRendererTextureUnit_CrtMask = 20,      // sampler2D crtMaskTile
    // The reflection pass's inputs (its two programs draw rectangles and bind these by hand: the 2D batch binds units 0
    // to 4), past every unit a lit program reads, so the sprites and the rain drawn after the pass find their frame
    // textures as the lit passes bound them.
    WorldRendererTextureUnit_ReflectionWeights = 21,     // sampler2D reflectionWeightMap
    WorldRendererTextureUnit_ReflectionSky = 22,         // sampler2D reflectionSkyMap
    WorldRendererTextureUnit_ReflectionDirections = 23,  // sampler2D reflectionDirectionMap
    WorldRendererTextureUnit_ReflectionSceneColor = 24,  // sampler2D sceneColor
    WorldRendererTextureUnit_ReflectionSceneDepth = 25,  // sampler2D sceneDepth (the march and the blocks)
    WorldRendererTextureUnit_ReflectionBlocks = 26,      // sampler2D reflectionBlocks
    // The water programs' own textures (world_water, world_water_instanced: lit programs whose materials carry no maps,
    // drawn in the scene pass before the reflection pass): a water material's maps, bound per material, and the copies of
    // the scene's colour and depth, bound for the pass. Past every unit the other programs read.
    WorldRendererTextureUnit_WaterNormalA = 27,          // sampler2D waterNormalA
    WorldRendererTextureUnit_WaterNormalB = 28,          // sampler2D waterNormalB
    WorldRendererTextureUnit_WaterFoam = 29,             // sampler2D waterFoam
    WorldRendererTextureUnit_WaterSceneColor = 30,       // sampler2D waterSceneColor
    WorldRendererTextureUnit_WaterSceneDepth = 31,       // sampler2D waterSceneDepth
} WorldRendererTextureUnit;

_Static_assert(WorldRendererTextureUnit_ShadowLevels >= WORLD_RENDERER_MATERIAL_MAP_COUNT,
    "renderer-owned texture units must sit above raylib's material map units");
_Static_assert(WorldRendererTextureUnit_TerrainRoughnessAo < WorldRendererTextureUnit_ShadowLevels,
    "the terrain's arrays sit below the frame textures");
_Static_assert(WORLD_SHADOW_MAX_LEVELS == (unsigned int)CONFIG_MAX_SHADOW_LEVELS,
    "the config asks for at most as many shadow levels as there may be");
_Static_assert(WorldRendererTextureUnit_HazeVolume < WorldRendererTextureUnit_TerrainAlbedoHeight,
    "the haze's volume sits below the terrain's arrays (the terrain program reads both)");
_Static_assert((WorldRendererTextureUnit_SkyVisibility != WorldRendererTextureUnit_HazeVolume)
    && (WorldRendererTextureUnit_SkyVisibility < WorldRendererTextureUnit_TerrainAlbedoHeight),
    "the sky's visibility map has a unit of its own in every lit program, the terrain's included");
_Static_assert(WORLD_RENDERER_HAZE_SLICE_COUNT == WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT,
    "the public haze slice count is the volume's");
_Static_assert((WorldRendererTextureUnit_RainMap >= WORLD_RENDERER_MATERIAL_MAP_COUNT)
    && (WorldRendererTextureUnit_RainMap > WorldRendererTextureUnit_AmbientOcclusion),
    "the rain's units sit above raylib's material maps and every other frame texture");
_Static_assert((WorldRendererTextureUnit_ScreenLight > WorldRendererTextureUnit_RainSkyView)
    && (WorldRendererTextureUnit_CrtMask > WorldRendererTextureUnit_ScreenLight),
    "the screens' units sit above the rain's");
_Static_assert(WorldRendererTextureUnit_ReflectionWeights > WorldRendererTextureUnit_CrtMask,
    "the reflection pass's units sit above every lit program's");
_Static_assert((WorldRendererTextureUnit_WaterNormalA > WorldRendererTextureUnit_ReflectionBlocks)
    && (WorldRendererTextureUnit_WaterSceneDepth < 48),
    "the water's units sit above the reflection pass's, within the 48 combined units GL 3.3 guarantees");

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

/** @brief One shadow level in a frame, in render space (WorldRendererShadows_PrepareFrame, from
 *  WorldShadowLevels_BeginFrame). */
typedef struct WorldRendererShadowLevelFrameStruct
{
    /** @brief Whether its map is drawn this frame; otherwise the lit shaders read it as it was drawn in an earlier frame,
     *  with these matrices (built again for this frame's origin). */
    bool IsRendered;
    /** @brief The light's view (looking along the light at the box's centre: the camera's position snapped to the
     *  level's texel grid when it was drawn) and its orthographic projection. */
    Matrix View;
    Matrix Projection;
    /** @brief Render space -> the level's light clip (raymath order: MatrixMultiply(View, Projection)): the lit shaders'
     *  shadowLevelMatrices[level]. */
    Matrix ViewProjection;
    /** @brief The box its casters are culled against while IsRendered: its four sides and its far plane (from
     *  ViewProjection); its near plane culls nothing, since what lies nearer the light is drawn onto it (depth
     *  clamping). */
    WorldFrustum Box;
    /** @brief Its texel's width, in metres: the size cut (WorldShadowLevels_IsLargeEnough) and the biases. */
    float Texel;
    /** @brief The distance its casters pick their LODs at (WorldShadowLevels_GetLodDistance), before the zoom factor. */
    float LodDistance;
    /** @brief The lookups' depth bias in its depth units (half a texel over its depth range) and their move off a surface
     *  at grazing incidence, in metres (a texel and a half). */
    float DepthBias;
    float NormalOffset;
} WorldRendererShadowLevelFrame;

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
    /** @brief The light the shadow levels are rendered from this frame (the brighter one once it stands high enough), its
     *  unit direction, and how strong its shadows are (0..1: faded near the horizon and while the lights hand over). The
     *  levels only run while ShadowLight is not None (AreShadowLevelsActive). */
    WorldShadowLight ShadowLight;
    Vector3 ShadowDirection;
    float ShadowFade;
    /** @brief The shadow levels' settings this frame (the player's quality and the world's reach), the levels used (the
     *  first ShadowLevelCount of ShadowLevels) and each one's matrices, box and lookups (WorldRendererShadows_PrepareFrame);
     *  valid while AreShadowLevelsActive. */
    WorldShadowLevelsSettings ShadowLevelSettings;
    uint32_t ShadowLevelCount;
    WorldRendererShadowLevelFrame ShadowLevels[WORLD_RENDERER_MAX_SHADOW_LEVELS];
    /** @brief The cube shadow's light: its position in render space (its RenderPosition), and the far range its packed
     *  distances are normalized by (its reach, at least twice the faces' near plane), which is also the reach its
     *  casters must touch; valid while IsPointShadowActive. */
    Vector3 PointShadowPosition;
    float PointShadowFar;
    /** @brief Lighting shared by the scene and crisp passes (computed once per render): the scene luminance estimate of
     *  the sky's light at the camera (WorldSkyLight_GetSceneLuminance), the field luminance night vision compares with
     *  before the lamps (NightVision_GetFieldLuminance of the daylight's and the night lights' shares of the light the eye
     *  is in), and the shadow strength. */
    float BaseSceneLuminance;
    float NightVisionField;
    float ShadowStrength;

    /** @brief Whether each optional pass runs this render: the shadow levels of the sun or the moon, the cube shadow of the
     *  nearest shadow-casting lamp, the AO. */
    bool AreShadowLevelsActive;
    bool IsPointShadowActive;
    bool IsAoActive;
    /** @brief Whether the contact shadows run (the shadow levels run and they are switched on), and whether the ambient
     *  pre-pass runs for them or the AO: its target then holds this render's values for the lit passes. */
    bool AreContactShadowsActive;
    bool IsAmbientPrepassActive;
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
    /** @brief The weather this frame (WorldWeatherLight_GetFrame in BuildFrame; the deck's light added with the sky's
     *  light, WorldRendererLighting_PrepareSkyLight): the clouds' cover and what it leaves of the sun and the moon, the
     *  deck's light, a flash's, the wetness, and the wind (its level direction, its speed and gust in metres per second,
     *  for what sways in it). */
    WorldWeatherLight Weather;
    /** @brief The clouds this frame (WorldRendererClouds_PrepareFrame, then WorldRendererClouds_PrepareLight from the sky's
     *  light): whether they are drawn (the pass is available, the player's quality and the world allow them, the
     *  atmosphere's tables run and a layer has coverage), their layers, look and patterns' anchors, the light that lights
     *  them (the sun, or the moon while the sun's light at their base is the weaker: its direction and its light above the
     *  air, the surfaces' units) and whether their shadow map is drawn (that light above the horizon) with its frame. While
     *  they are drawn the air is lit by the lights above the air in full above the clouds' base and by CloudLightShare of
     *  them below it (the weather's DirectShare; 1 while they are not drawn, when the lights are dimmed by it everywhere),
     *  and SkyObjectShare dims the discs, the stars and the Milky Way (1 while the clouds hide them per pixel,
     *  DirectShare otherwise). IsCloudDepthActive: the clouds in front of the scene's surfaces are drawn too (the
     *  G-buffer, drawn then, gives the march its surfaces' distances; the composite lays the clouds over them). */
    bool AreCloudsActive;
    bool IsCloudDepthActive;
    WorldCloudsFrame Clouds;
    bool IsCloudLightMoon;
    Vector3 CloudLightDirection;
    float CloudLightRadiance[3];
    bool IsCloudShadowActive;
    WorldCloudShadowFrame CloudShadow;
    float CloudLightShare;
    float SkyObjectShare;
    bool IsHazeActive;
    /** @brief The rain this frame (WorldRendererRain_PrepareFrame): its values from the environment (WorldRain_GetFrame);
     *  whether it is drawn at all (switched on; IsRainActive false draws no streaks, splashes, puddles or rain map, and
     *  every surface is wet alike); whether the rain map is read this frame, with its view, projection and view-projection
     *  (render space: the box it was drawn with, which may be a kept one) and the direction toward the rain it was drawn
     *  along (valid while it is read); whether the frame draws it, with its box (the visibility's; valid while it is
     *  drawn); whether the streaks and the splashes are drawn; the lightning's bolt, and whether it is drawn and lights
     *  the world. */
    WorldRainFrame Rain;
    bool IsRainActive;
    bool IsRainMapActive;
    bool IsRainMapDrawn;
    Vector3 RainMapTowardRain;
    Matrix RainMapView;
    Matrix RainMapProjection;
    Matrix RainMapViewProjection;
    WorldFrustum RainMapBox;
    bool AreRainStreaksActive;
    bool AreRainSplashesActive;
    WorldRainBolt Bolt;
    bool IsBoltActive;
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
    /** @brief Interior lighting this frame (WorldRendererInterior_PrepareCamera, WorldRendererSkyVisibility_PrepareFrame):
     *  whether it runs (switched on), whether the sky's visibility map is drawn, its matrices and
     *  box (valid while it is), the camera's interior (its openness and the zones at it, eased; not started while
     *  interior lighting is off), the light the eye is in at the camera (WorldInterior_GetCameraLuminance: the scene
     *  luminance and its daylight part, which the eye's anchor and real scale and night vision's field take) and the
     *  zones' exposure bias at the camera (stops; 0 while off). */
    bool IsInteriorActive;
    bool IsSkyVisibilityActive;
    WorldInteriorSkyMap SkyMap;
    WorldFrustum SkyMapBox;
    /** @brief The occupancy volume's window's bottom this frame, render space (WorldInterior_GetSkyWindowBottom). */
    float SkyVolumeBottom;
    /** @brief Whether the ambient pre-pass reads the sky's visibility for the camera's pixels (its target's G): the volume
     *  is drawn and the pre-pass is available. The lit passes then take it from there instead of reading it themselves. */
    bool IsSkyVisibilityOnScreen;
    WorldInteriorCamera CameraInterior;
    float CameraSceneLuminance;
    float CameraDayLuminance;
    float ExposureBias;
    /** @brief Whether the reflection pass runs this frame (WorldRendererReflections_Decide: it is available, switched on,
     *  the player's quality is not off and something in view mirrors: an object with has_reflections, or wet surfaces
     *  while the rain is on): the scene pass's opaque world then hands each mirroring pixel's data to the scene target's
     *  reflection attachments, and the pass adds what it mirrors. With it the march's settings at the player's quality,
     *  the shown objects that mirror, and whether wet surfaces do. */
    bool AreReflectionsActive;
    WorldReflectionsMarchSettings ReflectionMarch;
    size_t MirroringObjectCount;
    bool AreWetSurfacesMirroring;
    /** @brief The water this frame (WorldRendererWater_PrepareFrame): whether its pass runs (its program loaded and a
     *  model or a copies' cell in view has water slots), how many of those are in view and how many of them mirror their
     *  surroundings (a water material with has_reflections: the reflection pass then runs), whether the pass copies the
     *  scene for what lies under the water (the scene's HDR tier with a samplable depth and the copies made; otherwise
     *  the water shows its colour alone, as deep water), and the wind's speed its waves take (gust in). */
    bool IsWaterActive;
    size_t WaterObjectCount;
    size_t WaterMirrorCount;
    bool IsWaterBottomActive;
    float WaterWindSpeed;
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
 * @brief A run of casters' dither in one shader (WorldRendererCommon_SetRunDither): the fade it uploaded last, so a run of
 *        casters with the same fade (most are solid) uploads it once. Start each run zeroed: nothing uploaded yet.
 */
typedef struct WorldRendererDitherRunStruct
{
    bool _hasFade;
    float _fade;
} WorldRendererDitherRun;

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

/** @brief Which of a model's meshes a geometry pass draws: its world model's water slots are drawn by the water pass and
 *  the G-buffer alone. */
typedef enum
{
    /** @brief Every mesh but the water slots' (the zero value: every pass that does not say). */
    WorldRendererMeshSet_Solid = 0,
    /** @brief Only the water slots' meshes (the water pass with its program, the G-buffer as plain surfaces). */
    WorldRendererMeshSet_Water = 1,
} WorldRendererMeshSet;

/** @brief A water program (WorldRendererWater.c), declared below. */
typedef struct WorldRendererWaterProgramStruct WorldRendererWaterProgram;

/**
 * @brief The shaders one geometry pass draws an object's meshes with: its rigid meshes with Rigid, its skinned ones
 *        (GameSkeleton_IsMeshSkinned) with Skinned while the object has a pose (WorldRenderModel.Pose) that Palette can
 *        hold, and which of its meshes (Meshes; the water slots' with Water's water material in the water pass). A plain
 *        value type; everything in it is borrowed for the pass.
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
    /** @brief Which meshes it draws (zeroed: every mesh but the water slots'). */
    WorldRendererMeshSet Meshes;
    /** @brief With Meshes Water in the water pass: the program the water slots' meshes draw with, each with its water
     *  material (Rigid is its pass shader); NULL elsewhere (the G-buffer draws them with Rigid as any surface). */
    const WorldRendererWaterProgram* Water;
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
 * @brief The uniform locations of a shader that includes shaders/common/interior.glsl (through common/lighting): whether
 *        interior lighting runs, the sky's visibility map (its switch, sampler and matrix), the draw's ambient zones
 *        (their count and arrays, set per draw) and the debug view. -1 for a uniform the shader lacks.
 *
 * Filled by WorldRendererInterior_PrepareShader, which also points the map's sampler at its fixed unit;
 * WorldRendererInterior_UploadFrame uploads a frame's values, WorldRendererInterior_UploadZones a draw's zones.
 */
typedef struct WorldRendererInteriorLocationsStruct
{
    int _locActive;
    int _locSkyVisibilityActive;
    int _locSkyVisibilityMap;
    int _locSkyVisibilityViewProjection;
    int _locSkyVolumeBottom;
    int _locZoneCount;
    int _locZoneCenters;
    int _locZoneAxisX;
    int _locZoneAxisY;
    int _locZoneAxisZ;
    int _locZoneLights;
    int _locZoneSettings;
    int _locView;
    int _locViewScale;
} WorldRendererInteriorLocations;

/**
 * @brief The uniform locations of the clouds' shadow in a shader that includes shaders/common/lighting.glsl: whether it
 *        applies, its map's sampler, the map's frame (its centre, the inverse of its side, the plane's height), the light
 *        it was made for, the share of that light beyond the map, and whether that light is the moon. -1 for a uniform the
 *        shader lacks.
 *
 * Filled by WorldRendererClouds_PrepareLitShader, which also points the sampler at WorldRendererTextureUnit_CloudShadow;
 * WorldRendererClouds_UploadLitFrame uploads a frame's values.
 */
typedef struct WorldRendererCloudShadowLocationsStruct
{
    int _locActive;
    int _locMap;
    int _locFrame;
    int _locLight;
    int _locFallback;
    int _locIsMoon;
    int _locTop;
} WorldRendererCloudShadowLocations;

/**
 * @brief The uniform locations of the rain in a shader that includes shaders/common/rain.glsl and common/puddles.glsl
 *        (through common/lighting): the rain map (its switch, sampler, matrix and lookups' offsets), the puddles (their
 *        switch, the mask's sampler and frame, the ripples' frame and rate), the sky mirrored in water (the clear share,
 *        the deck, the moon's half), the bolt's light and the debug view. -1 for a uniform the shader lacks.
 *
 * Filled by WorldRendererRain_PrepareLitShader, which also points the samplers at their fixed units;
 * WorldRendererRain_UploadLitFrame uploads a frame's values.
 */
typedef struct WorldRendererRainLocationsStruct
{
    int _locMapActive;
    int _locMap;
    int _locMapMatrix;
    int _locMapOffsets;
    int _locPuddleActive;
    int _locPuddleMask;
    int _locPuddleFrame;
    int _locRippleFrame;
    int _locActive;
    int _locTop;
    int _locReflectionClearShare;
    int _locReflectionDeck;
    int _locReflectionMoon;
    int _locBoltLight;
    int _locBoltDirection;
    int _locView;
} WorldRendererRainLocations;

/**
 * @brief The uniform locations of a draw's screen light in a shader that includes shaders/common/lighting.glsl: whether a
 *        screen lights the draw, where its glass's centre is and the way it faces, its shape (its area, its reach, the
 *        mipmap level its picture's mean is read at, its whiteness), its glow and its picture's sampler. -1 for a
 *        uniform the shader lacks.
 *
 * Filled by WorldRendererSurfaces_PrepareLitShader, which also points the sampler at
 * WorldRendererTextureUnit_ScreenLight; WorldRendererSurfaces_UploadScreenLight uploads a draw's light.
 */
typedef struct WorldRendererScreenLightLocationsStruct
{
    int _locActive;
    int _locPosition;
    int _locNormal;
    int _locShape;
    int _locGlow;
    int _locPicture;
} WorldRendererScreenLightLocations;

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
    /** @brief The weather's wetness of surfaces. */
    int _locSurfaceWetness;
    /** @brief Screen-space AO: the AO map, its active flag and the scene resolution used to map gl_FragCoord to the AO
     *  map's UV. */
    int _locAmbientOcclusionMap;
    int _locSkyVisibilityOnScreen;
    int _locAoMapActive;
    /** @brief Per draw: whether the ambient pre-pass saw the draw's surface (WorldRendererLighting_SetDrawPlace). */
    int _locDrawInPrepass;
    int _locAoResolution;
    /** @brief The shadow levels: how many are read, each one's matrices and lookups' offsets, where a level's lookups end
     *  and its band starts, the log of the factor between the levels, the reach's fade, the maps' texel, the maps'
     *  sampler, the debug view; the light they were rendered from and how much they shadow the sun and the moon. */
    int _locShadowLevelCount;
    int _locShadowLevelMatrices;
    int _locShadowLevelOffsets;
    int _locShadowLevelEdge;
    int _locShadowLevelBandStart;
    int _locShadowLevelLogFactor;
    int _locShadowReachFade;
    int _locShadowLevels;
    int _locShadowLevelView;
    int _locShadowStrength;
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
     *  WorldRendererLighting_SetDrawPlace). */
    int _locHazeViewProjection;
    int _locHazeMultiplier;
    /** @brief The reflections: whether the pass's reflection pass adds what its surfaces mirror (they hand it their data;
     *  uploaded per pass) and whether the draw mirrors its surroundings (its placement's has_reflections, set before every
     *  lit draw: WorldRendererLighting_SetDrawPlace). */
    int _locReflectionsDeferred;
    int _locHasReflections;
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
    /** @brief Interior lighting (common/interior): the sky's visibility map on WorldRendererTextureUnit_SkyVisibility and
     *  the draw's ambient zones. */
    WorldRendererInteriorLocations _interior;
    /** @brief The clouds' shadow (common/lighting's CloudShadowAt): its switch, its map on
     *  WorldRendererTextureUnit_CloudShadow, the map's frame, its light, its share beyond the map and whether it shades
     *  the moon. */
    WorldRendererCloudShadowLocations _cloudShadow;
    /** @brief The rain (common/rain, common/puddles): the rain map on WorldRendererTextureUnit_RainMap, the puddle mask on
     *  WorldRendererTextureUnit_PuddleMask, the sky view on WorldRendererTextureUnit_RainSkyView, the puddles, the sky
     *  mirrored in water, the bolt's light and the debug view. */
    WorldRendererRainLocations _rain;
    /** @brief The draw's screen light (common/lighting's ScreenLight): its picture on WorldRendererTextureUnit_ScreenLight,
     *  uploaded with the forward point lights. */
    WorldRendererScreenLightLocations _screenLight;
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
    /** @brief The material's porosity and puddles (its params, GameModel.h), uploaded per material too. */
    int _locMaterialPorosity;
    int _locMaterialPuddles;
    /** @brief Whether the albedo map and the emissive map hold a picture stored bottom row first (a surface's render
     *  target), uploaded per material too (WorldRendererMapOrientation). */
    int _locIsAlbedoMapBottomUp;
    int _locIsEmissiveMapBottomUp;
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
 * @brief An asset the world's environment names: a texture (the Milky Way, the moon's face, the rain's streak and puddle
 *        mask) or a sprite animation (the rain's splash, the lightning's bolts); a slot holds one kind, the one its sync
 *        function loads.
 *
 * Each slot holds its asset under its own asset user, so replacing one slot's asset can never release another's (two
 * slots may name the same asset), and loads only when the environment names another asset: a failed name is logged once
 * and not retried until the name changes (WorldRendererCommon_SyncTextureSlot, WorldRendererCommon_SyncAnimationSlot).
 */
typedef struct WorldRendererAssetSlotStruct
{
    /** @brief The texture or the sprite animation, held under _user; NULL = unavailable (not loaded yet, or the name
     *  failed), and always NULL for the kind the slot does not hold. */
    GameTexture* _texture;
    SpriteAnimation* _animation;
    /** @brief The asset user holding the asset; minted on the first load, retired with the renderer. */
    AssetUserID _user;
    /** @brief The name the asset was loaded (or tried) for, NUL-terminated (unsigned char); valid while _isSynced. */
    GenericBuffer _name;
    /** @brief Whether a name has been tried. */
    bool _isSynced;
} WorldRendererAssetSlot;

/** @brief The atmospheric sky pass, with the moon, the Milky Way and light pollution's skyglow. */
typedef struct WorldRendererSkyStateStruct
{
    /** @brief The atmospheric sky shader drawn behind the geometry; NULL = fall back to the gradient clear. */
    GameShader* _shader;
    /** @brief The Milky Way panorama the world names (MilkyWayTexture); without it, no Milky Way. */
    WorldRendererAssetSlot _milkyWay;
    /** @brief The moon's face the world names (MoonTexture); without it the disc is a flat grey. */
    WorldRendererAssetSlot _moon;
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
    /** @brief The weather: the clouds' deck at the zenith, a flash's light and its direction. */
    int _locCloudZenithRadiance;
    int _locFlashIrradiance;
    int _locFlashDirection;
    int _locCloudHeight;
    int _locCloudsActive;
    int _locCloudLayer;
    int _locCloudLayerResolution;
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
    int _skyViewLocCloudSkyIrradiance;
    int _skyViewLocCloudAmbientRadiance;
    int _skyViewLocCloudHeight;
    int _skyViewLocCloudLightShare;
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
    int _volumeLocCloudSkyIrradiance;
    int _volumeLocCloudAmbientRadiance;
    int _volumeLocCloudHeight;
    int _volumeLocCloudLightShare;
    int _volumeLocDirectLightShares;
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

/**
 * @brief The uniform locations of a shader that includes shaders/common/clouds.glsl: the noise's samplers, the layers,
 *        the erosion, the patterns' anchors and tiles. -1 for a uniform the shader lacks.
 *
 * Filled by the cloud pass at load, which also points the samplers at their fixed units.
 */
typedef struct WorldRendererCloudLocationsStruct
{
    int _locShapeNoise;
    int _locDetailNoise;
    int _locWeatherMap;
    int _locLowLayer;
    int _locLowExtinction;
    int _locDetailStrength;
    int _locMidLayer;
    int _locMidExtinction;
    int _locHighLayer;
    int _locShapeAnchor;
    int _locDetailAnchor;
    int _locCoverageAnchor;
    int _locMidAnchor;
    int _locHighAnchor;
    int _locTiles;
    int _locMapTiles;
} WorldRendererCloudLocations;

/**
 * @brief The clouds (WorldRendererClouds.c): the march's, the accumulation's and the shadow map's shaders and locations,
 *        the noise (two 3D textures through GLTexture3D, the weather map a raylib texture, made at load from
 *        CloudNoise.h), the cloud target (RGBA16F at the quality's resolution: what the clouds add, their
 *        transmittance), the accumulation's two targets and the shadow map, the player's quality, the patterns' drift
 *        with the wind and the frame count for the steps' jitter.
 */
typedef struct WorldRendererCloudStateStruct
{
    /** @brief The march (world_clouds); NULL = no clouds (the weather's deck is drawn instead). */
    GameShader* _marchShader;
    WorldRendererAtmosphereLocations _marchAtmosphere;
    WorldRendererHazeLocations _marchHaze;
    WorldRendererCloudLocations _marchClouds;
    int _marchLocResolution;
    int _marchLocInvViewProj;
    int _marchLocSceneDepth;
    int _marchLocHasSceneDepth;
    int _marchLocSceneDepthResolution;
    int _marchLocInvProjection;
    int _marchLocDepthReversed;
    int _marchLocSunDirection;
    int _marchLocMoonDirection;
    int _marchLocLightDirection;
    int _marchLocLightRadiance;
    int _marchLocSunRadiance;
    int _marchLocMoonRadiance;
    int _marchLocGroundIrradiance;
    int _marchLocTownIrradiance;
    int _marchLocTint;
    int _marchLocForward;
    int _marchLocFlashRadiance;
    int _marchLocFlashPosition;
    int _marchLocMaxDistance;
    int _marchLocLowSteps;
    int _marchLocLightSteps;
    int _marchLocMidSteps;
    int _marchLocFrame;
    /** @brief The shadow map's builder (world_clouds_shadow); NULL = no cloud shadows (the sun's light is dimmed by the
     *  clouds' share everywhere). */
    GameShader* _shadowShader;
    WorldRendererAtmosphereLocations _shadowAtmosphere;
    WorldRendererCloudLocations _shadowClouds;
    int _shadowLocResolution;
    int _shadowLocCenter;
    int _shadowLocExtent;
    int _shadowLocPlane;
    int _shadowLocLightDirection;
    int _shadowLocSteps;
    /** @brief The accumulation over frames (world_clouds_resolve); NULL = each frame's march is shown as it is. */
    GameShader* _resolveShader;
    WorldRendererCloudLocations _resolveClouds;
    WorldRendererAtmosphereLocations _resolveAtmosphere;
    int _resolveLocCurrent;
    int _resolveLocHistory;
    int _resolveLocResolution;
    int _resolveLocInvViewProj;
    int _resolveLocPreviousViewProjection;
    int _resolveLocOriginShift;
    int _resolveLocHasHistory;
    int _resolveLocCurrentShare;
    /** @brief The clouds over the scene's surfaces (world_clouds_composite); NULL = the clouds lie behind every surface
     *  (the march's texels are all the sky's). */
    GameShader* _compositeShader;
    int _compositeLocLayer;
    int _compositeLocLayerResolution;
    int _compositeLocSceneDepth;
    int _compositeLocResolution;
    int _compositeLocDepthReversed;
    /** @brief The GL functions for the 3D textures, the shape's and the erosion's volumes (GL names, 0 none) and the
     *  weather map; _hasNoise once all three are made. */
    GLTexture3DApi _api;
    unsigned int _shapeNoise;
    unsigned int _detailNoise;
    Texture2D _weatherMap;
    bool _hasNoise;
    /** @brief The cloud target (made with the scene's targets, at the quality's share of the scene's size: _width and
     *  _height, the size last tried, made or not, so a target the GPU refuses is not tried again every frame) and the
     *  shadow map (made once). */
    RenderTexture2D _target;
    bool _hasTarget;
    bool _isTargetTried;
    int _width;
    int _height;
    /** @brief The accumulated results (two, used in turn: the last one and this frame's; made with the cloud target, the
     *  same size), which of them holds the newest, whether it holds this frame's (else the target does), and the last
     *  frame's view-projection and origin the newest was made with (none after a new target or a frame without clouds). */
    RenderTexture2D _history[2];
    bool _hasHistory;
    int _newest;
    bool _isResolved;
    bool _hasPreviousFrame;
    Matrix _previousViewProjection;
    Vector3Double _previousOrigin;
    RenderTexture2D _shadowMap;
    bool _hasShadowMap;
    /** @brief The player's quality (CONFIG_CLOUD_QUALITY_*: the config's, or WorldRenderer_SetCloudQuality's). */
    int32_t _quality;
    /** @brief The patterns' drift with the wind (advanced by each frame's real seconds) and the frames drawn (the steps'
     *  jitter). */
    WorldCloudsDrift _drift;
    uint32_t _frameIndex;
    /** @brief The pass's CPU time in its last render, milliseconds (the status line). */
    double _lastMilliseconds;
    /** @brief Set once a failure has been logged: the noise (or its GL functions), the cloud target, the accumulation's
     *  targets, the shadow map. */
    bool _isFailureReported;
    bool _isTargetFailureReported;
    bool _isHistoryFailureReported;
    bool _isShadowMapFailureReported;
} WorldRendererCloudState;

/**
 * @brief One of the rain's programs (the streaks, the splashes, the lightning's bolt): a lit program (common/lighting:
 *        the frame's light, the haze, the night vision, the rain map) with its own uniforms. -1 for a location the
 *        program lacks.
 */
typedef struct WorldRendererRainProgramStruct
{
    /** @brief The shader (held under the shader user); NULL = unavailable (that part of the rain is not drawn). */
    GameShader* _shader;
    /** @brief Its frame-lighting locations (WorldRendererLighting_PrepareLitShader). */
    WorldRendererLitLocations _lit;
    /** @brief The scene's view-projection, the target's size, the rain's colour (the streaks and the splashes). */
    int _locViewProjection;
    int _locResolution;
    int _locColor;
    /** @brief The streaks: the anchor, the fall's drift, the wind, the fall speed, the exposure, the share drawn. */
    int _locAnchor;
    int _locFall;
    int _locWind;
    int _locFallSpeed;
    int _locExposure;
    int _locStreakShare;
    /** @brief The splashes: the grid's first cell and corner, the cycles run, the share and the cycle's length, the rain
     *  map's fall (its slant), the camera's axes; the animation's life, the frame a draw shows, the frames, that frame's
     *  area in its texture and its width over its height. */
    int _locSplashCell;
    int _locSplashCorner;
    int _locSplashCycles;
    int _locSplashShare;
    int _locSplashPeriod;
    int _locSlant;
    int _locCameraRight;
    int _locCameraUp;
    int _locSplashLife;
    int _locFrame;
    int _locFrameCount;
    int _locFrameArea;
    int _locFrameAspect;
    /** @brief The bolt: its foot, top and width, its frame's area in its texture, its radiance. */
    int _locBoltFoot;
    int _locBoltTop;
    int _locBoltWidth;
    int _locBoltArea;
    int _locBoltRadiance;
} WorldRendererRainProgram;

/** @brief The rain (WorldRendererRain.c; the rain's model is WorldRain.h's). */
typedef struct WorldRendererRainStateStruct
{
    /** @brief The rain map: its GL functions (loaded with the shaders), its one-layer array and whether it exists, its
     *  box's grid from frame to frame (snapped to its texels), and whether a failure to make it was logged. */
    GLShadowArrayApi _api;
    GLShadowArray _map;
    bool _hasMap;
    bool _isMapFailureReported;
    WorldRenderCascadeGrid _mapGrid;
    /** @brief The streaks', the splashes' and the bolt's programs. */
    WorldRendererRainProgram _streaks;
    WorldRendererRainProgram _splashes;
    WorldRendererRainProgram _bolt;
    /** @brief The streaks' mesh (every streak's seed and corners), the splashes' (every cell's corners); valid while their
     *  flags are set. The bolt draws with the sprites' quad. */
    Mesh _streakMesh;
    bool _hasStreakMesh;
    Mesh _splashMesh;
    bool _hasSplashMesh;
    /** @brief What the world names: the streak's texture, the splash's sprite animation, the puddle mask's texture, the
     *  bolts' sprite animation; and the splash animation last checked for an fps and a frame count a splash plays (each
     *  loaded animation is checked, and a problem logged, once). */
    WorldRendererAssetSlot _streakTexture;
    WorldRendererAssetSlot _splashAnimation;
    WorldRendererAssetSlot _puddleMask;
    WorldRendererAssetSlot _boltAnimation;
    const SpriteAnimation* _checkedSplashAnimation;
    /** @brief How far the rain has drifted (WorldRain.h), advanced by each frame's real seconds. */
    WorldRainDrift _drift;
    /** @brief The rain map kept between its draws (WorldRain.h): the box it was last drawn with; forgotten (zeroed) on
     *  every frame that does not read it. */
    WorldRainMapKeep _mapKeep;
    /** @brief The A/B (on by default: off draws no rain and wets every surface alike) and the debug view. */
    bool _isEnabled;
    WorldRendererRainView _view;
    /** @brief Whether the last render read the rain map (drawn then or kept), drew the streaks, the splashes and a bolt
     *  (the status). */
    bool _wasMapRead;
    bool _wereStreaksDrawn;
    bool _wereSplashesDrawn;
    bool _wasBoltDrawn;
} WorldRendererRainState;

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
    int _locCloudsActive;
    int _locCloudLayer;
    int _locCloudLayerResolution;
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
    /** @brief The sprite as the shadow levels' light sees it (a billboard faces that light); the camera transform while
     *  the levels do not run or for a fixed sprite. */
    Matrix ShadowTransform;
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
    int _locIsCard;
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

/** @brief The shadow levels of the sky's shadow light (the sun by day, the moon on bright nights; WorldShadowLevels.h). */
typedef struct WorldRendererShadowLevelStateStruct
{
    /** @brief The depth-only shader the levels draw with, and its dither uniforms; NULL = no sun or moon shadows. */
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
    /** @brief The GL functions of the levels' maps (GLShadowArray_LoadApi, at shader load). */
    GLShadowArrayApi _api;
    /** @brief The levels' maps: one layer per level the reach uses (of the player's config's levels), of its map size
     *  (valid while _hasMaps; made again when either changes: WorldRendererShadows_PrepareFrame). */
    GLShadowArray _maps;
    bool _hasMaps;
    /** @brief The map size and layer count making the maps last failed for (logged once; not tried again until the
     *  config asks for others); 0 for none. */
    int _failedMapSize;
    uint32_t _failedLayerCount;
    /** @brief The levels from frame to frame (WorldShadowLevels: each level's texel grid, the light and the place its map
     *  was drawn for, the schedule). Zeroed with the renderer; one for the renderer's one camera, like the LOD states. */
    WorldShadowLevels _levels;
    /** @brief The world the levels last ran for and whether they ran the frame before: a frame of another world, or after
     *  frames without them, renders every level again (WorldShadowLevels_Invalidate). */
    const World* _world;
    bool _ranLastFrame;
    /** @brief Whether the debug view tints every lit surface by the level its shadow comes from. Off by default. */
    bool _isLevelViewEnabled;
} WorldRendererShadowLevelState;

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
    int _locContactLightDirection;
    int _locContactLength;
    int _locSkyVisibilityActive;
    int _locSkyVolume;
    int _locSkyViewProjection;
    int _locSkyVolumeBottom;
    int _locViewToRender;
    /** @brief The blur after it (the raw pre-pass into _target, the 4 x 4 pattern averaged away); NULL = no AO. Its
     *  uniform locations follow. */
    GameShader* _blurShader;
    int _blurLocResolution;
    int _blurLocInvProjection;
    int _blurLocDepthTexture;
    int _blurLocNormalTexture;
    int _blurLocDepthReversed;
    /** @brief The raw pre-pass's low-res target (R the occlusion, G the sky's visibility, B the contact light; the
     *  pattern still in it), which the blur reads. */
    RenderTexture2D _rawTarget;
    /** @brief Whether _rawTarget has been created. */
    bool _hasRawTarget;
    /** @brief The low-res target the lit passes sample (R = 1 open .. <1 occluded, for the ambient term; G and B as
     *  the raw target's, blurred and passed through). */
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
    /** @brief The shadow passes' own: the shadow distance with its fade band, the size cut, the LOD distance and the
     *  shadow LOD (common/instancing's instanceShadow* uniforms). */
    int _locShadowDistance;
    int _locShadowFadeStart;
    int _locShadowFadeBand;
    int _locShadowMinRadius;
    int _locShadowLodDistance;
    int _locShadowLod;
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
/** @brief A program's uniforms for drawing the sky's occupancy volume (shaders/sky_slices: the map's eye height and depth
 *  range, the window's bottom, the drawing's mode). */
typedef struct WorldRendererSliceLocationsStruct
{
    int _locEyeHeight;
    int _locDepth;
    int _locBottom;
    int _locMode;
} WorldRendererSliceLocations;

typedef struct WorldRendererInstanceStateStruct
{
    /** @brief The lit twin's instancing uniforms (its program is _pbr._instanced). */
    WorldRendererInstancingLocations _pbrInstancing;
    /** @brief The water's twin's instancing uniforms (its program is _water._instanced). */
    WorldRendererInstancingLocations _waterInstancing;
    /** @brief The G-buffer's twin (world_normal_instanced), its outline flag and its uniforms; NULL = copies draw one at a
     *  time into the G-buffer. */
    GameShader* _normalShader;
    int _normalLocOutlineFlag;
    int _normalLocHazeMultiplier;
    WorldRendererDitherLocations _normalDither;
    WorldRendererInstancingLocations _normalInstancing;
    /** @brief The shadow levels' twin (world_depth_instanced); NULL = copies cast one at a time into the levels. */
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
    /** @brief The sky's occupancy volume's twin (world_sky_slices_instanced) and its slice uniforms; NULL = copies are
     *  drawn one at a time into the volume. */
    GameShader* _skySliceShader;
    WorldRendererSliceLocations _skySliceLocations;
    WorldRendererDitherLocations _skySliceDither;
    WorldRendererInstancingLocations _skySliceInstancing;
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

/** @brief The sky's occupancy volume (WorldRendererSkyVisibility.c): what keeps the sky out around the camera, as height
 *  slices on the map's grid (WorldInterior.h's layout), with an OR pyramid in its levels. */
typedef struct WorldRendererSkyVisibilityStateStruct
{
    /** @brief The direct GL calls the volume needs (GLSliceMap.h). */
    GLSliceMapApi _api;
    /** @brief The occupancy volume: an integer texture (GL_RGBA32UI, WORLD_INTERIOR_SKY_MAP_RESOLUTION a side) whose
     *  levels hold its pyramid, a framebuffer for each level, and whether they all exist. */
    unsigned int _volume;
    unsigned int _framebuffers[WORLD_INTERIOR_SKY_LEVEL_COUNT];
    bool _hasVolume;
    /** @brief A level-0-sized scratch volume and its framebuffer: one layer's parity is drawn there, then ORed into the
     *  volume (WorldRendererSkyVisibility.c: things that overlap keep their own parity). */
    unsigned int _scratch;
    unsigned int _scratchFramebuffer;
    /** @brief The frame's layer of each model the volume draws (uint8_t, parallel to its visibility list) and their boxes
     *  in render space (BoundingBox), kept from frame to frame so they stop allocating once grown. */
    GenericBuffer _modelLayers;
    GenericBuffer _modelBoxes;
    /** @brief The programs it is drawn with (world_sky_slices and its skinned twin; the instanced twin is the instances'
     *  state's) and the pyramid's (world_sky_reduce); without the first or the last there is no sky visibility. */
    GameShader* _shader;
    GameShader* _skinnedShader;
    GameShader* _reduceShader;
    /** @brief The level shader's step (2: a level of the pyramid; 1: a layer copied into the volume). */
    int _reduceLocStep;
    /** @brief Their slice uniforms, their dithers and the skinned twin's palette. */
    WorldRendererSliceLocations _locations;
    WorldRendererSliceLocations _skinnedLocations;
    WorldRendererDitherLocations _dither;
    WorldRendererDitherLocations _skinnedDither;
    WorldRendererPalette _skinnedPalette;
    /** @brief Set once a failure has been logged. */
    bool _isFailureReported;
} WorldRendererSkyVisibilityState;

/** @brief Interior lighting's state (WorldRendererInterior.c). */
typedef struct WorldRendererInteriorStateStruct
{
    /** @brief Whether interior lighting is switched on (the A/B; on by default). */
    bool _isEnabled;
    /** @brief Whether it runs this frame (switched on): the draws upload their zones only then (set by
     *  WorldRendererInterior_PrepareCamera). */
    bool _isActive;
    /** @brief How open the camera's surroundings are, as the game last measured it (WorldRenderer_SetCameraOpenness; 1
     *  until it does). */
    float _cameraOpenness;
    /** @brief The camera's interior, eased from frame to frame (restarted while interior lighting is off). One for the
     *  renderer's one camera, like the LOD states. */
    WorldInteriorCamera _camera;
    /** @brief The debug view. */
    WorldRendererInteriorView _view;
} WorldRendererInteriorState;

/** @brief One picture handed over for a surface (WorldRenderer_SetSurfaceTexture), held for one render. */
typedef struct WorldRendererSurfaceBindingStruct
{
    /** @brief The screen's object. */
    uint64_t ObjectId;
    /** @brief The surface's name (a copy). */
    unsigned char SurfaceName[WORLD_MODEL_SURFACE_NAME_MAX_LENGTH + 1U];
    /** @brief The picture (its owner's texture, borrowed for the render). */
    Texture2D Texture;
    /** @brief Whether its first row is its picture's bottom (a render target drawn with raylib). */
    bool IsBottomUp;
} WorldRendererSurfaceBinding;

/**
 * @brief One screen's light in a render (WorldRendererSurfaces_PrepareFrame): a shown screen or CRT that glows with a
 *        picture handed over for it, in render space.
 */
typedef struct WorldRendererScreenLightStruct
{
    /** @brief The screen's object. */
    uint64_t ObjectId;
    /** @brief Its glass's centre and the way its front faces (unit). */
    Vector3 Position;
    Vector3 Normal;
    /** @brief Its glass's area (square metres) and how far its light reaches (metres). */
    float Area;
    float Range;
    /** @brief Its glow per unit of its picture's colour: the surface's emission times the component's brightness, the
     *  surface's light intensity and its power (a CRT's animation and flicker). */
    float Glow;
    /** @brief How white its light is (a CRT's squeezed beam), 0..1. */
    float Whiteness;
    /** @brief Its picture (borrowed for the render) and the mipmap level whose four by four taps give its mean colour (0
     *  without mipmaps). */
    Texture2D Picture;
    float Level;
} WorldRendererScreenLight;

/** @brief The surfaces' pictures and the screens' light (WorldRendererSurfaces.c). */
typedef struct WorldRendererSurfaceStateStruct
{
    /** @brief The pictures handed over for the next render (WorldRendererSurfaceBinding), forgotten after it. */
    GenericBuffer _bindings;
    /** @brief This render's screen lights (WorldRendererScreenLight), built by WorldRendererSurfaces_PrepareFrame. */
    GenericBuffer _lights;
} WorldRendererSurfaceState;

/**
 * @brief The uniform locations of a CRT program's own uniforms (shaders/crt/fragment.frag, shaders/common/crt.glsl), set
 *        per CRT draw except the pass's scale: the picture's shape (curvature, aspect, width, height in pixels), its look
 *        (scanline strength, 0 without scanlines; mask strength; glow; vignette), its power animation (the open part's
 *        half width and height, its brightness with the flicker in, its whiteness), its mask (the kind, the tile's
 *        sampler, its mean colour and whether it is sRGB), its glass's reflection, and the pass's window pixels per pixel.
 *        -1 for a uniform the shader lacks.
 */
typedef struct WorldRendererCrtLocationsStruct
{
    int _locShape;
    int _locLook;
    int _locPower;
    int _locMaskKind;
    int _locMaskTile;
    int _locMaskMean;
    int _locMaskIsSrgb;
    int _locReflection;
    int _locWindowPixelScale;
} WorldRendererCrtLocations;

/** @brief The CRT surfaces' programs (WorldRendererCrt.c): the lit programs a CRT's glass draws with in the camera's lit
 *  passes (the scene and the crisp overlay), the PBR program's uniforms and more. */
typedef struct WorldRendererCrtStateStruct
{
    /** @brief The rigid program (world_crt) and the skinned one (world_crt_skinned: its fragment stage over the PBR's
     *  skinned vertex stage), each with the PBR program's locations; a shader NULL = unavailable (a CRT draws as a plain
     *  screen with the PBR program; without the rigid one the skinned one is not loaded). */
    WorldRendererPbrProgram _rigid;
    WorldRendererPbrProgram _skinned;
    /** @brief Their CRT uniforms. */
    WorldRendererCrtLocations _rigidCrt;
    WorldRendererCrtLocations _skinnedCrt;
    /** @brief Whether every CRT shows _maskOverride instead of its own mask (WorldRenderer_SetCrtMaskOverride). */
    bool _isMaskOverridden;
    CrtMaskKind _maskOverride;
    /** @brief The render clock of the frame being drawn (its flicker), set with each pass's frame uniforms. */
    float _timeSeconds;
} WorldRendererCrtState;

/**
 * @brief Which of a draw's material maps hold a picture stored bottom row first (a surface's render target, whose first
 *        row is the bottom of what was drawn into it): the lit shaders read those with v flipped. False for every map a
 *        model brings.
 */
typedef struct WorldRendererMapOrientationStruct
{
    /** @brief The albedo map (a paper's picture). */
    bool IsAlbedoBottomUp;
    /** @brief The emissive map (a screen's picture). */
    bool IsEmissiveBottomUp;
} WorldRendererMapOrientation;

/** @brief The number of reflection attachments the scene target gets (attachments 1 to 3: the weights, the sky, the
 *  directions with the screen's share; common/reflection_output). */
#define WORLD_RENDERER_REFLECTION_DATA_COUNT 3

/** @brief The screen-space reflections (WorldRendererReflections.c; the march's maths are WorldReflections.h's). */
typedef struct WorldRendererReflectionStateStruct
{
    /** @brief The pass's program (world_reflections; NULL: unavailable) and its uniforms' locations (-1 when absent). */
    GameShader* _shader;
    int _locSceneSize;
    int _locBlockCount;
    int _locViewProjection;
    int _locInvViewProjection;
    int _locCameraForward;
    int _locNearFar;
    int _locResolutionDivisor;
    int _locMaxBlocks;
    int _locSunDirection;
    int _locSunDiscCos;
    int _locMoonDirection;
    int _locMoonDiscCos;
    int _locView;
    int _locViewScale;
    int _locDepthReversed;
    /** @brief The scene target's reflection attachments (RGBA16F at the scene's size: colour attachments 1 to 3 of its
     *  framebuffer, owned here: raylib's unload frees only the colour and the depth), 0 while not made, and whether all
     *  of them are attached (only in the scene's HDR tier with a samplable depth). */
    unsigned int _dataTextures[WORLD_RENDERER_REFLECTION_DATA_COUNT];
    bool _hasData;
    /** @brief The block pass's program (world_reflection_blocks; NULL: the reflections are unavailable) and its uniforms'
     *  locations. */
    GameShader* _blockShader;
    int _blockLocSceneSize;
    int _blockLocDepthReversed;
    /** @brief The block image (R32F, a texel a WORLD_REFLECTIONS_BLOCK_SIZE-pixel block of the scene: each block's
     *  nearest surface's depth) and whether it exists. */
    RenderTexture2D _blocks;
    bool _hasBlocks;
    /** @brief The pass's target (RGBA16F, the scene's size over the divisor it was made for) and whether it exists. */
    RenderTexture2D _target;
    bool _hasTarget;
    int32_t _targetDivisor;
    /** @brief The scene's size (the attachments, the block image and the target are made for it). */
    int _width;
    int _height;
    /** @brief Set when making the attachments or the targets failed for this scene size (no retry until the scene
     *  target is made again or the quality goes off and back on). */
    bool _isMakeFailed;
    /** @brief The player's quality (CONFIG_REFLECTION_QUALITY_*), the A/B switch (on by default) and the debug view. */
    int32_t _quality;
    bool _isEnabled;
    WorldRendererReflectionView _view;
    /** @brief Whether the opaque world's draws are writing the attachments now (between BeginSurfaces and EndSurfaces,
     *  outside a pause), and whether a pause stopped them (PauseSurfaces, until ResumeSurfaces). */
    bool _areSurfacesWriting;
    bool _areSurfacesPaused;
    /** @brief Set once a failure (the attachments, the target) has been logged. */
    bool _isFailureReported;
} WorldRendererReflectionState;

/**
 * @brief The uniform locations of a water program's own uniforms (shaders/water/fragment.frag, shaders/common/water.glsl).
 *        A water material's, set per draw of a material: its maps' switches, the maps' frames (each the offset and the
 *        turn) and one over their tiles, whether they lie on the texture coordinates, the waves' strength with the wind
 *        in, the colour (linear), the absorption, the roughness, the reflectance facing it, the foam's depth and
 *        strength, its ripples and reflections. The pass's: whether the copies hold what lies under the water, the
 *        inverse of the scene's view-projection (the bottom's place from its depth), the depth convention. The samplers
 *        are pointed at their units at load. -1 for a uniform the shader lacks.
 */
typedef struct WorldRendererWaterLocationsStruct
{
    int _locHasNormalA;
    int _locHasNormalB;
    int _locHasFoam;
    int _locMapA;
    int _locMapB;
    int _locMapFoam;
    int _locInverseTiles;
    int _locIsAlongUv;
    int _locWaveStrength;
    int _locColor;
    int _locAbsorption;
    int _locRoughness;
    int _locReflectance;
    int _locFoamShape;
    int _locHasRipples;
    int _locHasReflections;
    int _locHasBottom;
    int _locInvViewProjection;
    int _locDepthReversed;
} WorldRendererWaterLocations;

/** @brief One water program (world_water, or its instanced twin world_water_instanced): a lit program (the PBR
 *  program's locations, dither and pass shader; its materials carry no maps) with the water's own uniforms, and the
 *  water material whose uniforms it holds this frame. */
struct WorldRendererWaterProgramStruct
{
    /** @brief The lit program; its shader NULL = unavailable. */
    WorldRendererPbrProgram _program;
    /** @brief Its water uniforms. */
    WorldRendererWaterLocations _water;
    /** @brief The water material its uniforms were last set for, NULL from each frame's start (the maps' frames move
     *  every frame). */
    const WaterMaterial* _uploadedMaterial;
};

/** @brief The water (WorldRendererWater.c; the moving patterns' maths are WorldWater.h's). */
typedef struct WorldRendererWaterStateStruct
{
    /** @brief The programs for the models' water slots and for the copies' (instanced); the rigid one NULL = no water is
     *  drawn (the slots are left out of every pass), the instanced one NULL = copies draw one at a time. */
    WorldRendererWaterProgram _rigid;
    WorldRendererWaterProgram _instanced;
    /** @brief The copies of the scene's colour and depth (a target made as the scene's: RGBA16F and the camera's depth
     *  texture), made when water first needs them for the scene's size, and whether they exist; whether making them
     *  failed for this size (no retry until the scene target is made again), and the size. */
    RenderTexture2D _copies;
    bool _hasCopies;
    bool _isCopyFailed;
    int _width;
    int _height;
    /** @brief How far every water's patterns have run (WorldWater.h). */
    WorldWaterDrift _drift;
    /** @brief The frame's origin and wind, for the maps' frames of the materials drawn. */
    Vector3Double _origin;
    float _windSpeed;
    /** @brief The draws of the last render: one per LOD drawn of each model with water slots, one per cell of copies
     *  with them. */
    size_t _drawCount;
    /** @brief Set once a skinned water mesh, and a failure of the copies, have been logged. */
    bool _isSkinnedReported;
    bool _isCopyFailureReported;
} WorldRendererWaterState;

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
    /** @brief Toggle for the contact shadows (A/B; on by default). */
    bool _areContactShadowsEnabled;
    /** @brief Toggle for the eye's night vision (A/B; on by default); the world's NightVisionStrength scales it. */
    bool _isNightVisionEnabled;
    /** @brief Toggle for sprite lighting (A/B; on by default): off draws every sprite unlit at its full colours. */
    bool _isSpriteLightingEnabled;
    /** @brief The night vision's calibration on the game's light scale (NightVision_GetCalibration), computed once. */
    NightVisionCalibration _nightVision;
    /** @brief The per-object render cache and this render's model/sprite/light lists. */
    WorldRenderCache _cache;
    /** @brief This render's visibility: per pass the cache entries it draws (the camera's frustum, each shadow level's box,
     *  the cube's reach, the draw distances and opacities), per entry its fades. Built right after the cache's sync. */
    WorldVisibility _visibility;
    /** @brief Toggle for the camera's frustum culling and the shadow levels' box culling (A/B; on by default). Off draws
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
    WorldRendererShadowLevelState _shadowLevels;
    WorldRendererPointShadowState _pointShadow;
    WorldRendererGBufferState _gbuffer;
    WorldRendererAoState _ao;
    WorldRendererOutlineState _outline;
    WorldRendererBloomState _bloom;
    WorldRendererSunshaftState _sunshaft;
    WorldRendererCrispState _crisp;
    WorldRendererTerrainState _terrain;
    WorldRendererInstanceState _instances;
    WorldRendererSkyVisibilityState _skyVisibility;
    WorldRendererInteriorState _interior;
    WorldRendererCloudState _clouds;
    WorldRendererRainState _rain;
    WorldRendererSurfaceState _surfaces;
    WorldRendererCrtState _crt;
    WorldRendererReflectionState _reflections;
    WorldRendererWaterState _water;
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

/** @brief Config-side shadow distance scale (the config's shadow_distance_scale; 1.0 when no config is bound). */
float WorldRendererCommon_ConfigShadowDistanceScale(const WorldRenderer* self);

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
 * @brief Uploads a caster's dither in a run of a depth pass (its fade, no LOD cross-fade), unless the run's last upload
 *        into the shader was the same fade. An upload leaves @p shader bound.
 * @param shader The depth shader (its program).
 * @param locations Its dither locations (WorldRendererCommon_PrepareDither).
 * @param run The run's state in this shader (zeroed at the run's start).
 * @param fade The caster's fade.
 */
void WorldRendererCommon_SetRunDither(Shader shader, const WorldRendererDitherLocations* locations,
    WorldRendererDitherRun* run, float fade);

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
 * @brief Draws one mesh into a depth pass (the shadow levels, the cube shadow, the sky's occupancy volume) as raylib's
 *        DrawMesh does, with the same matrices multiplied in the same order (the model's matrix, the view and the
 *        projection uploaded where @p shader has them, then the MVP), but without what no depth stage reads (the
 *        material's colours and maps, the normal matrix) and without unbinding the program and the vertex array after
 *        the draw, nor binding the program, which the caller binds once for a run of meshes in one shader
 *        (rlEnableShader): a pass of many small meshes costs a fraction of DrawMesh's. Call
 *        WorldRendererCommon_EndDepthMeshes after the pass's last one, before anything else draws. A mesh without a
 *        vertex array draws nothing.
 * @param shader The depth shader (its program), bound; the caller uploads its other uniforms (the dither, a palette)
 *        first.
 * @param mesh The mesh, uploaded.
 * @param transform Its matrix into render space.
 */
void WorldRendererCommon_DrawDepthMesh(Shader shader, Mesh mesh, Matrix transform);

/** @brief Ends a run of WorldRendererCommon_DrawDepthMesh: unbinds the vertex array, the buffers and the program, as
 *  DrawMesh does after each mesh. */
void WorldRendererCommon_EndDepthMeshes(void);

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

/** @brief An empty asset slot: no asset, no asset user yet, an empty name. */
void WorldRendererCommon_ConstructAssetSlot(WorldRendererAssetSlot* slot);

/** @brief Retires a slot's asset user (releasing its asset) and frees its name. */
void WorldRendererCommon_DeconstructAssetSlot(WorldRenderer* self, WorldRendererAssetSlot* slot);

/**
 * @brief Makes a slot hold the texture the environment names: when the name differs from the one the slot was synced
 *        for, its old texture is released and the new one loaded under the slot's own user (minted on the first load).
 *        A failure is logged by the loader once for the name, and the slot stays empty until the name changes.
 * @param self The renderer.
 * @param slot The slot.
 * @param name The texture's asset name (NULL changes nothing).
 * @param featureName What goes without it, for the loader's warning.
 */
void WorldRendererCommon_SyncTextureSlot(WorldRenderer* self, WorldRendererAssetSlot* slot, const unsigned char* name,
    const unsigned char* featureName);

/**
 * @brief Makes a slot hold the sprite animation the environment names, as WorldRendererCommon_SyncTextureSlot does a
 *        texture (the animation's frames' textures are its dependencies, held with it).
 * @param self The renderer.
 * @param slot The slot.
 * @param name The sprite animation's asset name (NULL changes nothing).
 * @param featureName What goes without it, for the loader's warning.
 */
void WorldRendererCommon_SyncAnimationSlot(WorldRenderer* self, WorldRendererAssetSlot* slot, const unsigned char* name,
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
 * @brief Creates a depth-only framebuffer whose depth attachment is a samplable texture (in .depth; raylib's own depth
 *        format), read with point filtering.
 * @param width The target's width in pixels.
 * @param height Its height.
 * @returns The target, or a zero target (id 0) if the framebuffer could not be made complete.
 */
RenderTexture2D WorldRendererCommon_CreateDepthTarget(int width, int height);

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
 * WATER. Only the meshes of @p shaders' set are drawn (WorldRendererMeshSet: by default every mesh but the world model's
 * water slots'). In the water pass (the water set with a water program) each water slot's mesh draws with that program
 * and its water material (WorldRendererWater_PrepareMaterial), rigid (a skinned one in its bind pose, logged once).
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

/**
 * @brief Draws one LOD of a cached model into a depth pass (the shadow levels, the cube shadow, the sky's occupancy
 *        volume): WorldRendererGeometry_DrawModel's meshes, transform and skinning, each mesh through
 *        WorldRendererCommon_DrawDepthMesh (no material: depth stages read none), binding the program at the first mesh
 *        and whenever the next mesh's differs. The caller uploads the dither first and calls
 *        WorldRendererCommon_EndDepthMeshes after the pass's last mesh.
 * @param model The cached model; must have an asset.
 * @param lod The LOD to draw; one past the last draws nothing.
 * @param shaders The pass's depth shaders; must not be NULL, and nothing draws without a rigid one.
 */
void WorldRendererGeometry_DrawModelDepth(const WorldRenderModel* model, size_t lod,
    const WorldRendererMeshShaders* shaders);

/**
 * @brief Whether a mesh of a cached model belongs to a pass's set: a water slot's mesh (its material slot is one of its
 *        world model's water slots) to the water set alone, every other mesh to the solid set.
 * @param model The cached model (one without an asset has no water slots).
 * @param materialIndex The mesh's material index (raylib's meshMaterial).
 * @param set The pass's set.
 * @returns Whether the pass draws the mesh.
 */
bool WorldRendererGeometry_IsMeshInSet(const WorldRenderModel* model, int materialIndex, WorldRendererMeshSet set);

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


// WorldRendererSurfaces.c
/**
 * @brief Holds a surface's picture for the next render (WorldRenderer_SetSurfaceTexture): a second picture for the same
 *        object's surface replaces the first.
 * @param self The renderer.
 * @param objectId The screen's object; not 0.
 * @param surfaceName The surface's name; not NULL, at most WORLD_MODEL_SURFACE_NAME_MAX_LENGTH bytes.
 * @param texture The picture; its id not 0.
 * @param isBottomUp Whether its first row is its picture's bottom (a render target).
 * @returns Success; ErrorCode_IllegalArgument for an id of 0, a NULL name or a texture id of 0;
 *          ErrorCode_ArgumentOutOfRange for a name longer than a surface's can be.
 */
Error WorldRendererSurfaces_SetTexture(WorldRenderer* self, uint64_t objectId, const unsigned char* surfaceName,
    Texture2D texture, bool isBottomUp);

/** @brief Forgets the pictures handed over: they held for one render (a screen whose owner stops handing one over shows
 *  none, never a stale or freed texture). */
void WorldRendererSurfaces_ForgetTextures(WorldRenderer* self);

/**
 * @brief Puts the picture handed over for a model entry's surface into the per-draw copy of that surface's material:
 *        a screen's as its emissive map, white at the entry's SurfaceEmission while it is on (off, or without a
 *        picture, it gives off no light: its material's own surface, the dark glass); a CRT's the same while it is on or
 *        still turning off (its power transition above 0: the CRT program plays the animation on it); a paper's as its
 *        albedo map (without a picture, its material's own). Call it only for the surface's material slot.
 * @param self The renderer.
 * @param model The entry; its Surface must not be NULL.
 * @param material [in,out] The per-draw copy (its maps a copy too).
 * @returns Which of its maps now hold a picture stored bottom row first.
 */
WorldRendererMapOrientation WorldRendererSurfaces_Apply(const WorldRenderer* self, const WorldRenderModel* model,
    Material* material);

/** @brief Counts the drawn model entries with a surface, those with a picture handed over for it and those whose surface
 *  is a CRT (the status). */
void WorldRendererSurfaces_Count(const WorldRenderer* self, uint32_t* outSurfaceCount, uint32_t* outPictureCount,
    uint32_t* outCrtCount);

/**
 * @brief Caches a lit shader's screen light uniforms (shaders/common/lighting.glsl) and points its picture's sampler at
 *        WorldRendererTextureUnit_ScreenLight (once, at load).
 * @param shader The shader (its program).
 * @param outLocations [out] Receives the locations (-1 for a uniform the shader lacks).
 */
void WorldRendererSurfaces_PrepareLitShader(Shader shader, WorldRendererScreenLightLocations* outLocations);

/**
 * @brief Builds the render's screen lights from the render cache's shown entries: every screen or CRT that glows with a
 *        picture handed over for it (on, or a CRT still turning on or off: its animation's light) and a light intensity
 *        above 0, its glass's centre, facing, area and reach in render space (the entry's matrix, its normals through
 *        the inverse transpose), its glow and its picture's mipmap level. Every shown screen counts, in view or not:
 *        a monitor behind the camera still lights the desk in front of it.
 * @param self The renderer; its cache synced for @p frame and the pictures handed over.
 * @param frame The frame (its clock: a CRT's flicker).
 */
void WorldRendererSurfaces_PrepareFrame(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Chooses the screen light of a draw: the nearest of the render's screen lights (by its glass's centre) whose reach
 *        meets the draw's bounding sphere and which the sphere is not wholly behind.
 * @param self The renderer.
 * @param center The draw's bounding sphere's centre, in render space.
 * @param radius Its radius.
 * @returns The light (borrowed until the next WorldRendererSurfaces_PrepareFrame), or NULL: none reaches the draw.
 */
const WorldRendererScreenLight* WorldRendererSurfaces_FindScreenLight(const WorldRenderer* self, Vector3 center,
    float radius);

/**
 * @brief Uploads the screen light of the lit draw that follows (WorldRendererSurfaces_FindScreenLight), its picture
 *        bound on WorldRendererTextureUnit_ScreenLight; none (screenLightActive false) without one. Does nothing for a
 *        shader without the uniforms.
 * @param self The renderer.
 * @param shader The lit shader (its program).
 * @param locations Its screen light locations (WorldRendererSurfaces_PrepareLitShader).
 * @param center The draw's bounding sphere's centre, in render space.
 * @param radius Its radius.
 */
void WorldRendererSurfaces_UploadScreenLight(const WorldRenderer* self, Shader shader,
    const WorldRendererScreenLightLocations* locations, Vector3 center, float radius);


// WorldRendererCrt.c
/**
 * @brief Loads the CRT programs (world_crt and its skinned twin world_crt_skinned) as PBR programs
 *        (WorldRendererLighting_LoadPbrProgram) and caches their CRT uniforms, the mask tile's sampler pointed at
 *        WorldRendererTextureUnit_CrtMask. Without the rigid one (logged by the loader) CRTs draw as plain screens with
 *        the PBR program; without the skinned one a posed CRT's skinned meshes do.
 */
void WorldRendererCrt_LoadShaders(WorldRenderer* self);

/**
 * @brief Whether a model entry's mesh of one material slot draws with a CRT program: the entry shows a CRT surface on
 *        that slot and the CRT programs loaded.
 * @param self The renderer.
 * @param model The entry.
 * @param materialIndex The mesh's material slot.
 * @returns true for the CRT's glass.
 */
bool WorldRendererCrt_IsCrtMesh(const WorldRenderer* self, const WorldRenderModel* model, int materialIndex);

/**
 * @brief Whether a model entry shows a CRT that draws with the CRT programs (its surface is a CRT and they loaded).
 * @param self The renderer.
 * @param model The entry.
 * @returns true when some of its meshes draw with them.
 */
bool WorldRendererCrt_IsCrtEntry(const WorldRenderer* self, const WorldRenderModel* model);

/**
 * @brief Uploads the CRT programs' uniforms of a pass: its window pixels per pixel (1 at the window's resolution, as in
 *        the crisp overlay; the pixelated scene's more, which fades out the mask's window-pixel pattern), and keeps the
 *        frame's clock for the flicker. WorldRendererLighting_UploadFrame calls it.
 * @param self The renderer.
 * @param frame The frame.
 * @param windowPixelScale How many window pixels one of the pass's pixels spans (>= 1).
 */
void WorldRendererCrt_UploadPass(WorldRenderer* self, const WorldRenderFrame* frame, float windowPixelScale);

/**
 * @brief Uploads a CRT entry's own uniforms to one CRT program, for its meshes drawn next: the picture's shape and look,
 *        its power animation (WorldRendererCrt_GetPowerShape), its mask (the renderer's override while one is set) with
 *        the tile bound on WorldRendererTextureUnit_CrtMask, and its glass's reflection.
 * @param self The renderer.
 * @param program The CRT program (rigid or skinned).
 * @param locations Its CRT locations.
 * @param model The entry; WorldRendererCrt_IsCrtEntry must hold.
 */
void WorldRendererCrt_UploadDraw(const WorldRenderer* self, const WorldRendererPbrProgram* program,
    const WorldRendererCrtLocations* locations, const WorldRenderModel* model);

/**
 * @brief A CRT entry's power animation at a moment (CrtScreen_GetPowerShape on its power state and transition), with its
 *        flicker (CrtScreen_GetFlicker) in its brightness and its light.
 * @param model The entry; its Surface must be a CRT.
 * @param timeSeconds The render clock.
 * @returns The shape.
 */
CrtPowerShape WorldRendererCrt_GetPowerShape(const WorldRenderModel* model, float timeSeconds);


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
 *        the shadow levels' light and the cube shadow's light while those run) and the draw order of the blended and additive
 *        sprites the camera's passes draw, back to front.
 *
 * Needs the frame's View, AreShadowLevelsActive / ShadowDirection, IsPointShadowActive / ShadowLightIndex and the
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
 * @brief The opaque part of the lit sprite draws of a scene or crisp pass, for the camera's sprites passing @p filter: the
 *        cutout sprites (opaque, writing depth, dithered by their fade), with the opaque world. Call after the opaque
 *        models, with the frame's lighting uploaded (WorldRendererLighting_UploadFrame), inside BeginMode3D. Restores
 *        culling.
 * @param self The renderer (its sprite matrices prepared for this frame: WorldRendererSprites_PrepareFrame).
 * @param filter Which sprites (by their effective OmitPixelation).
 * @param applyScreenAo Whether the pass's AO map lines up with it (the scene pass; not the window-resolution crisp
 *        pass).
 */
void WorldRendererSprites_DrawLitOpaque(WorldRenderer* self, WorldRendererPixelFilter filter, bool applyScreenAo);

/**
 * @brief The see-through part of the lit sprite draws of a scene or crisp pass: the decals (over the opaque world,
 *        writing no depth), then the blended and additive sprites back to front (writing no depth, their draw-distance
 *        fade in their alpha). Call after the opaque world (in the scene pass after the reflections, so a decal covers
 *        what its surface mirrors), inside BeginMode3D. Restores culling, the depth mask and the blend mode.
 * @param self The renderer (its sprite matrices prepared for this frame).
 * @param filter Which sprites (by their effective OmitPixelation).
 * @param applyScreenAo As WorldRendererSprites_DrawLitOpaque's.
 */
void WorldRendererSprites_DrawLitTransparent(WorldRenderer* self, WorldRendererPixelFilter filter, bool applyScreenAo);


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
 * @brief Loads one program that shades meshes the PBR program's way (its material uniforms, common/lighting, the dither,
 *        a palette when its vertex stage skins: the PBR programs and the CRT programs): its locations, its location table
 *        with the material map samplers pointed at mraMap, normalMap and emissiveMap, and its pass shader.
 * @param self The renderer.
 * @param program [out] The program; its shader NULL when the asset did not load (logged by the loader).
 * @param assetName The shader asset's name.
 */
void WorldRendererLighting_LoadPbrProgram(WorldRenderer* self, WorldRendererPbrProgram* program,
    const unsigned char* assetName);

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
 * @brief Uploads the frame's lighting uniforms to every lit shader the renderer has (the PBR programs, the CRT programs,
 *        the sprite shader and the terrain's), with the pass's dither pixel scale and window pixel scale, and binds the
 *        shadow maps, the cube shadow and (when @p applyScreenAo) the AO map to their fixed units.
 *
 * Call before the lit models and sprites of a pass. The crisp overlay passes false for @p applyScreenAo: it renders at
 * window resolution, where the low-res AO map would not line up; for the same reason its @p ditherPixelScale is window /
 * scene (the scene pass's is 1), which keeps the dither on the scene's pixel grid, and its @p windowPixelScale 1 (the
 * scene pass's is window / scene: how many window pixels one of its pixels spans, which a CRT's mask needs).
 * @p areReflectionsDeferred: the pass's mirroring surfaces hand their reflections to the reflection pass (the scene pass
 * while it runs) instead of adding the sky's themselves (every other pass).
 */
void WorldRendererLighting_UploadFrame(WorldRenderer* self, const WorldRenderFrame* frame, bool applyScreenAo,
    bool areReflectionsDeferred, Vector2 ditherPixelScale, float windowPixelScale);

/**
 * @brief Uploads the frame's lighting uniforms to one more lit program (the rain's), as WorldRendererLighting_UploadFrame
 *        does to the renderer's own; the frame textures are those it bound.
 * @param self The renderer.
 * @param frame The frame.
 * @param shader The program.
 * @param locations Its locations (WorldRendererLighting_PrepareLitShader).
 * @param applyScreenAo Whether the pass reads the ambient pre-pass at the scene's pixels.
 * @param areReflectionsDeferred Whether its mirroring surfaces hand their reflections to the reflection pass.
 */
void WorldRendererLighting_UploadLitProgram(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererLitLocations* locations, bool applyScreenAo, bool areReflectionsDeferred);

/**
 * @brief Reach-culls the frame's forward lights against a bounding sphere in render space and uploads the strongest few
 *        to a lit shader, for the object drawn next, with the screen light that reaches it
 *        (WorldRendererSurfaces_UploadScreenLight).
 * @param self The renderer.
 * @param shader The lit shader (its program).
 * @param locations Its locations (WorldRendererLighting_PrepareLitShader).
 * @param center The object's bounding sphere's centre, in render space.
 * @param radius Its radius.
 */
void WorldRendererLighting_UploadObjectLights(WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, Vector3 center, float radius);

/**
 * @brief Sets what the lit draw that follows takes from its place: its placement's resolved haze multiplier (1 the
 *        real air's, 0 none), the ambient zones its sphere touches (WorldRendererInterior_UploadZones) and whether the
 *        ambient pre-pass saw its surface (the G-buffer drew it: solid below WORLD_VISIBILITY_GBUFFER_FADE_LIMIT; a
 *        decal lies on the surface it saw): if not, the draw takes no screen AO or contact shadow and reads the sky
 *        straight above itself; and whether its glossy surfaces mirror their surroundings (its placement's resolved
 *        has_reflections). Every lit draw sets them, lit or not (an unlit sprite is hazed too), as the uniforms keep the
 *        last draw's values.
 * @param self The renderer.
 * @param shader The lit shader (its program).
 * @param locations Its locations (WorldRendererLighting_PrepareLitShader).
 * @param center The draw's bounding sphere's centre, in render space.
 * @param radius Its radius.
 * @param haze The multiplier (>= 0; a negative or NaN one counts as 0 in the shader).
 * @param isInPrepass Whether the ambient pre-pass saw the draw's surface.
 * @param hasReflections Whether the draw mirrors its surroundings (false for sprites, the terrain and the rain).
 */
void WorldRendererLighting_SetDrawPlace(const WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, Vector3 center, float radius, float haze, bool isInPrepass,
    bool hasReflections);

/**
 * @brief Uploads one material's PBR scalar factors and map-presence flags to a PBR program, for its meshes drawn next.
 *
 * DrawMesh uploads colDiffuse and binds the maps itself, but not these. Pass the per-draw copy, after the variant and a
 * surface's picture were applied to it, so its factors and maps are the ones drawn.
 * @param program The program the meshes draw with (the rigid one or the skinned one).
 * @param material The material.
 * @param orientation Which of its maps hold a picture stored bottom row first (a surface's).
 */
void WorldRendererLighting_UploadMaterial(const WorldRendererPbrProgram* program, const Material* material,
    WorldRendererMapOrientation orientation);

/** @brief Logs a line about the frame's light at the camera (with the status, WorldRenderer_LogStatus): the sun's and the
 *  moon's through the air, the sky's irradiance, the ground's bounce, the night's light, the scene luminance and the
 *  exposure. */
void WorldRendererLighting_LogStatus(const WorldRenderer* self);


// WorldRendererShadows.c
/** @brief Loads the depth and cube-depth shaders (and their alpha-tested sprite variants), the shadow levels' GL functions
 *  and the shadow cube (the levels' maps are made by WorldRendererShadows_PrepareFrame, for the config's size). */
void WorldRendererShadows_LoadShaders(WorldRenderer* self);

/** @brief Unloads the shadow levels' maps and the shadow cube. Needs a live GL context. */
void WorldRendererShadows_Unload(WorldRenderer* self);

/**
 * @brief Picks the light that casts the cube shadow: the CastsShadows light with positive intensity and reach
 *        nearest the camera (the origin of render space: the shortest RenderPosition).
 * @returns Its index in the render cache's lights, or SIZE_MAX when no light qualifies.
 */
size_t WorldRendererShadows_SelectPointLight(const WorldRenderer* self);

/**
 * @brief Fills the frame's shadow values the passes and the visibility share, in render space: the shadow levels (while
 *        AreShadowLevelsActive: their maps made for the config's size and level count, then WorldShadowLevels_BeginFrame
 *        from frame->ShadowDirection and the frame's settings: which levels are used and which render, each one's
 *        matrices, box, LOD distance and lookups; AreShadowLevelsActive is turned off when the maps cannot be made) and
 *        the cube shadow's light position and far range (while IsPointShadowActive). Call after the shadow decisions,
 *        before the visibility; once per frame (it moves the levels' grids and their schedule).
 */
void WorldRendererShadows_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame);

/**
 * @brief Draws the depth of a pass's model casters (its visibility list) with a depth shader and its skinned twin, each
 *        dithered by its shadow fade (into the twin too, through @p skinnedDither, while the caster has a pose), solid
 *        (no cross-fade dither): a shadow changes once per change and never shimmers. Each casts with its own shadow LOD
 *        while it has one, otherwise the LOD it heads to, and in a level past the first at least the one it would show
 *        at the level's inner edge; a two-sided caster draws both faces. Ignores the pixelation filter. The meshes go
 *        through WorldRendererGeometry_DrawModelDepth, the dither only when it changes (WorldRendererCommon_SetRunDither),
 *        and the vertex array and the program are unbound at the end. The caller has set the target, the matrices and
 *        the culling (@p faceCulled, put back after each two-sided caster).
 * @param self The renderer.
 * @param shaders The depth shaders (a light's view: the whole bodies); must not be NULL.
 * @param dither The rigid shader's dither locations.
 * @param skinnedDither The skinned twin's.
 * @param pass The pass whose list is drawn.
 * @param level The shadow level drawn (its LOD distance), or NULL for the cube shadow.
 * @param zoomFactor The camera's zoom factor (the level's LOD distance is measured with it).
 * @param faceCulled The face the pass culls (RL_CULL_FACE_FRONT or RL_CULL_FACE_BACK).
 */
void WorldRendererShadows_DrawModelCasters(WorldRenderer* self, const WorldRendererMeshShaders* shaders,
    const WorldRendererDitherLocations* dither, const WorldRendererDitherLocations* skinnedDither,
    WorldVisibilityPass pass, const WorldRendererShadowLevelFrame* level, float zoomFactor, int faceCulled);

/**
 * @brief The shadow levels' settings for a frame: the player's quality (the config's level count, factor, map size and
 *        update periods; the defaults without a config) and the world's reach, brought into range.
 * @param self The renderer.
 * @param environment The world's environment; must not be NULL.
 * @returns The settings.
 */
WorldShadowLevelsSettings WorldRendererShadows_GetSettings(const WorldRenderer* self, const WorldEnvironment* environment);

/** @brief Whether the shadow levels can run at all: their depth shader and the GL functions of their maps are there (the
 *  maps themselves are made for the config's size at the first frame that runs them). */
bool WorldRendererShadows_AreLevelsAvailable(const WorldRenderer* self);

/** @brief Binds the shadow levels' maps on WorldRendererTextureUnit_ShadowLevels for the lit passes, while the levels run
 *  this frame. */
void WorldRendererShadows_BindLevels(const WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Records the frame's shadow levels in the renderer's status: how many were used, which rendered and how many
 *  casters each drew, their widths, the reach, the maps' size and the debug view. Needs the frame's visibility. */
void WorldRendererShadows_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Logs the shadow levels' status line (the levels used with their widths, which rendered and their casters, the
 *  maps' size, the reach); nothing before the first render or without a logger. */
void WorldRendererShadows_LogStatus(const WorldRenderer* self);

/** @brief Renders the shadow levels that render this frame (IsRendered) from the frame's shadow light into their layers of
 *  the maps, with depth clamping. Each draws its visibility list (the casters in its box large enough for its texels):
 *  models, the copies of model instances and terrain chunks (their faces turned away from the light, or both faces for
 *  two-sided casters), each at its level's LOD, and cutout sprites turned toward the light
 *  (WorldRendererSprites_PrepareFrame's ShadowTransform); each caster dithered by its shadow fade. */
void WorldRendererShadows_RenderLevels(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Renders the frame's shadow light into the six cube faces, drawing its visibility list: the models, the copies of
 *        model instances, the terrain chunks and the cutout sprites (turned toward the light) within its reach, each
 *        dithered by its shadow fade (two-sided casters with both faces).
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

/** @brief Runs the ambient pre-pass from the G-buffer into the AO target: the AO at frame->AoStrength, the contact
 *  shadows while frame->AreContactShadowsActive; then its blur. */
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


// WorldRendererClouds.c
/**
 * @brief Loads the clouds: their three shaders (each missing one logged by the loader), the GL functions for 3D
 *        textures, and the noise (CloudNoise.h, generated here: about 0.13 s) as two 3D textures and the weather map, and
 *        the shadow map's target. A failure is logged once; without the march, the noise or its GL functions there are no
 *        clouds; without the accumulation each frame's march shows as it is; without the shadow map's shader or target
 *        they cast no shadows. Needs a live GL context (from EnsureShaders).
 * @param self The renderer.
 */
void WorldRendererClouds_Load(WorldRenderer* self);

/**
 * @brief Releases the noise and the shadow map (WorldRenderer_Deconstruct).
 * @param self The renderer.
 */
void WorldRendererClouds_Unload(WorldRenderer* self);

/**
 * @brief Makes sure the cloud target matches the scene's size and the quality (made again when either changed; released
 *        while the quality is off): the scene's size at high quality, half of it at low, at most
 *        WORLD_RENDERER_CLOUD_MAX_WIDTH wide, with the accumulation's two targets; a failure is logged once (no clouds,
 *        or no accumulation) and not tried again until the size or the quality changes. Every render, after the scene's
 *        targets.
 * @param self The renderer.
 * @param sceneWidth The scene target's size, in pixels.
 * @param sceneHeight
 */
void WorldRendererClouds_EnsureTarget(WorldRenderer* self, int sceneWidth, int sceneHeight);

/** @brief Releases the cloud target. */
void WorldRendererClouds_ReleaseTargets(WorldRenderer* self);

/** @brief Whether the clouds can be drawn at all: the march and the noise exist, and the cloud target too unless the
 *  quality is off (then they are available but not drawn: the status's "inactive"). */
bool WorldRendererClouds_IsAvailable(const WorldRenderer* self);

/**
 * @brief Moves the clouds' patterns with the wind for the frame's real seconds and decides whether they are drawn this
 *        frame (frame->AreCloudsActive: available, the player's quality above off, the world's AreCloudsEnabled, the
 *        atmosphere's tables running, a layer with coverage), with their layers and anchors (frame->Clouds), and
 *        whether they are drawn over the scene's surfaces too (frame->IsCloudDepthActive: the composite's shader and a
 *        G-buffer with samplable depth). Before the sky's light (WorldRendererLighting_PrepareSkyLight takes the air's
 *        light from it) and before the G-buffer's decision (it is drawn for the clouds too).
 * @param self The renderer.
 * @param frame The frame (its origin and environment, the atmosphere's decisions).
 * @param deltaSeconds The frame's real seconds (none for a negative or non-finite one).
 */
void WorldRendererClouds_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame, double deltaSeconds);

/**
 * @brief Chooses the light that lights the clouds from the clear sky's light at their base (@p cloudTop: the sun, or the
 *        moon while the sun's is the weaker) and the shadow map's frame for it (frame->IsCloudShadowActive while the map
 *        exists and the light is above the horizon). From WorldRendererLighting_PrepareSkyLight, while the clouds are
 *        drawn.
 * @param self The renderer.
 * @param frame The frame.
 * @param cloudTop The clear sky's light at the clouds' base, for the lights above the air in full.
 */
void WorldRendererClouds_PrepareLight(WorldRenderer* self, WorldRenderFrame* frame, const WorldSkyLightFrame* cloudTop);

/**
 * @brief Draws the clouds' shadow map (while it is active) and marches the clouds into the cloud target (while they are
 *        drawn), after the atmosphere's tables, the haze's volume and the G-buffer, before the scene. Records the pass's
 *        CPU time.
 * @param self The renderer.
 * @param frame The frame.
 */
void WorldRendererClouds_Render(WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Lays the clouds in front of the scene's surfaces over them (the cloud target's texels of the geometry, the
 *        G-buffer's depth telling surfaces from the sky): target = target x their transmittance + their light, the
 *        target's alpha kept. After the scene pass and its outlines, before bloom, the sun shafts and the eye; only while
 *        frame->IsCloudDepthActive.
 * @param self The renderer.
 * @param frame The frame.
 * @param target The scene's HDR picture (the scene target, or the outlines' copy of it while they run).
 */
void WorldRendererClouds_Composite(WorldRenderer* self, const WorldRenderFrame* frame, RenderTexture2D target);

/**
 * @brief Binds the cloud target on WorldRendererTextureUnit_CloudLayer for the sky pass and the stars (manually, like the
 *        tables), while the clouds are drawn.
 * @param self The renderer.
 * @param frame The frame.
 */
void WorldRendererClouds_BindLayer(const WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Binds the clouds' shadow map on WorldRendererTextureUnit_CloudShadow for the lit passes, while it is active.
 * @param self The renderer.
 * @param frame The frame.
 */
void WorldRendererClouds_BindShadow(const WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Fills a lit shader's cloud-shadow locations and points its map's sampler at WorldRendererTextureUnit_CloudShadow.
 * @param shader The lit shader.
 * @param outLocations [out] Its locations.
 */
void WorldRendererClouds_PrepareLitShader(Shader shader, WorldRendererCloudShadowLocations* outLocations);

/**
 * @brief Uploads the frame's cloud shadow to a lit shader (off while it is not active).
 * @param shader The lit shader.
 * @param locations Its cloud-shadow locations.
 * @param frame The frame.
 */
void WorldRendererClouds_UploadLitFrame(Shader shader, const WorldRendererCloudShadowLocations* locations,
    const WorldRenderFrame* frame);

/**
 * @brief Logs the clouds' status line (whether they are drawn, the quality and the target's size, the layers, the light,
 *        the shadow map, the pass's CPU time).
 * @param self The renderer.
 */
void WorldRendererClouds_LogStatus(const WorldRenderer* self);


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
 *        against it): its level's (the anchor's before it started) in the light it is in this frame, with the zones'
 *        bias at the camera.
 * @param self The renderer.
 * @param frame The frame (CameraSceneLuminance, CameraDayLuminance and ExposureBias must be set).
 * @returns The exposure.
 */
float WorldRendererExposure_GetCurrent(const WorldRenderer* self, const WorldRenderFrame* frame);

/**
 * @brief Moves the eye on by a frame and returns the tonemap's exposure: takes the newest blocks the GPU has finished,
 *        measures them, eases the eye, then meters @p source for the frames to come and asks for its copy. Sets the
 *        frame's AdaptedLuminance, AdaptedRealLuminance and IsExposureMetered.
 * @param self The renderer.
 * @param frame The frame (CameraSceneLuminance, CameraDayLuminance, ExposureBias and SkyLight must be set).
 * @param source The finished HDR scene, as the tonemap reads it (the scene target or the outlines').
 * @param deltaSeconds Real seconds since the previous render; 0, negative or NaN holds the eye.
 * @returns The exposure (finite, within EYE_ADAPTATION_MIN_EXPOSURE and EYE_ADAPTATION_MAX_EXPOSURE).
 */
float WorldRendererExposure_Update(WorldRenderer* self, WorldRenderFrame* frame, RenderTexture2D source,
    float deltaSeconds);


// WorldRendererSkyVisibility.c
/** @brief Loads the sky's occupancy volume's programs and makes the volume (once, before the instanced twins: theirs sits
 *  beside its program); a failure is logged once and the sky's visibility stays off (every surface takes the whole sky's
 *  light). */
void WorldRendererSkyVisibility_Load(WorldRenderer* self);

/** @brief Frees the volume. Needs a live GL context. */
void WorldRendererSkyVisibility_Unload(WorldRenderer* self);

/** @brief Whether the volume can be drawn: it exists and its programs loaded. */
bool WorldRendererSkyVisibility_IsAvailable(const WorldRenderer* self);

/**
 * @brief Decides whether the volume is drawn this frame (interior lighting active and the volume available) and fills
 *        the frame's SkyMap and SkyMapBox around the camera (WorldInterior_GetSkyMap) and its window's bottom. Before the
 *        visibility, which lists what the volume draws by the map's box.
 * @param self The renderer.
 * @param frame The frame (IsInteriorActive and Origin must be set).
 */
void WorldRendererSkyVisibility_PrepareFrame(const WorldRenderer* self, WorldRenderFrame* frame);

/**
 * @brief Draws the volume: everything its visibility list holds (models and instance cells that occlude the sky, terrain
 *        chunks) from above, both faces, with no depth test, once toggling (XOR) the slices below each fragment, once
 *        setting (OR) the slice it passes through; each dithered by its fade. Then its pyramid, level by level. Call while
 *        frame->IsSkyVisibilityActive, before the ambient pre-pass and the lit passes.
 */
void WorldRendererSkyVisibility_Render(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Binds the volume on WorldRendererTextureUnit_SkyVisibility for the lit passes (while it is drawn this frame);
 *  bound manually, as the lit passes draw with DrawMesh. */
void WorldRendererSkyVisibility_BindMap(const WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief The volume as a texture a 2D pass binds with SetShaderValueTexture (its id; its size level 0's). */
Texture2D WorldRendererSkyVisibility_GetVolumeTexture(const WorldRenderer* self);


// WorldRendererInterior.c
/** @brief Sets interior lighting's state up (switched on, the camera open and not started). Called once by
 *  WorldRenderer_Create. */
void WorldRendererInterior_Construct(WorldRenderer* self);

/**
 * @brief Caches a lit shader's interior uniform locations (common/interior) and points the map's sampler at
 *        WorldRendererTextureUnit_SkyVisibility. Call once at load (from WorldRendererLighting_PrepareLitShader).
 * @param shader The shader (its program).
 * @param outLocations [out] Receives the locations.
 */
void WorldRendererInterior_PrepareShader(Shader shader, WorldRendererInteriorLocations* outLocations);

/**
 * @brief Decides whether interior lighting runs this frame and moves the camera's interior on: the zones at the camera
 *        (render space's origin) and the openness the game last gave, eased by @p deltaSeconds; restarted while
 *        interior lighting is off. Fills frame->IsInteriorActive, frame->CameraInterior and frame->ExposureBias. After the
 *        render cache's sync, before the sky's light (WorldRendererLighting_PrepareSkyLight takes the camera's light
 *        from it).
 * @param self The renderer.
 * @param frame The frame.
 * @param deltaSeconds Real seconds since the previous render.
 */
void WorldRendererInterior_PrepareCamera(WorldRenderer* self, WorldRenderFrame* frame, float deltaSeconds);

/**
 * @brief Uploads a frame's interior values to one lit shader: whether interior lighting runs, the sky's visibility map's
 *        switch and matrix, no zones (until a draw sets its own) and the debug view.
 * @param self The renderer.
 * @param frame The frame.
 * @param shader The shader (its program).
 * @param locations Its interior locations.
 */
void WorldRendererInterior_UploadFrame(const WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererInteriorLocations* locations);

/**
 * @brief Uploads the ambient zones a lit draw takes: those its sphere touches (WorldInterior_SelectZones over the render
 *        cache's zones, at most WORLD_INTERIOR_MAX_DRAW_ZONES), or none while interior lighting does not run.
 * @param self The renderer.
 * @param shader The shader (its program).
 * @param locations Its interior locations.
 * @param center The draw's sphere's centre, in render space.
 * @param radius Its radius.
 */
void WorldRendererInterior_UploadZones(const WorldRenderer* self, Shader shader,
    const WorldRendererInteriorLocations* locations, Vector3 center, float radius);

/** @brief Draws the ambient zones as wireframes in their light's colour (the debug views), inside the scene pass's 3D
 *  mode, in render space. */
void WorldRendererInterior_DrawZones(const WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Logs interior lighting's status line (from the last render's status): whether it runs, the map's occluders, the
 *  zones, the camera's openness and zones, the light the eye is in, the bias and the debug view. */
void WorldRendererInterior_LogStatus(const WorldRenderer* self);


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
 *        dithered by its shadow fade (by its fade in the sky's volume), through WorldRendererCommon_DrawDepthMesh with the
 *        program bound once. Inside the pass's matrices and culling.
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
 * @brief A shadow pass's copies (its visibility list of cells): each copy as the LOD with the larger share (in a shadow
 *        level past the first at least the one it would show at the level's inner edge; the component's own shadow LOD
 *        while it has one), dithered by its own shadow fade, left out of a level it is too small for; both faces for a
 *        two-sided component. Instanced through the pass's twin (world_depth_instanced, or world_cube_depth_instanced
 *        with the frame's cube light), or one at a time through @p shader. Inside the pass's matrices and culling.
 * @param self The renderer.
 * @param frame The frame (the zoom factor; the cube's light for PointShadow).
 * @param shader The pass's ordinary depth shader (its program).
 * @param dither Its dither locations.
 * @param pass The shadow pass.
 * @param level The shadow level drawn (its texel and LOD distance), or NULL for the cube shadow.
 */
void WorldRendererInstances_DrawCasters(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererDitherLocations* dither, WorldVisibilityPass pass, const WorldRendererShadowLevelFrame* level);

/**
 * @brief The water pass's copies: the camera's cells whose world model has water slots, each with its forward lights and
 *        draw place (by its sphere and its component's haze) and the LODs its copies show, their water slots' meshes with
 *        their water materials, instanced through world_water_instanced (each copy's fade and LOD band in the vertex
 *        stage), or one at a time through world_water while instancing is off or the twin or a buffer is missing. Inside
 *        the water pass (WorldRendererWater_Render).
 * @param self The renderer.
 * @param frame The frame (its zoom factor).
 * @returns How many cells' water was drawn.
 */
size_t WorldRendererInstances_DrawWater(WorldRenderer* self, const WorldRenderFrame* frame);

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


// WorldRendererReflections.c
/** @brief Starts the reflections' state: no program, attachments or target, switched on, the config's quality (high
 *  without a config), no debug view. */
void WorldRendererReflections_Construct(WorldRenderer* self);

/** @brief Loads the pass's programs (world_reflections, world_reflection_blocks), caches their uniforms and points their
 *  samplers at their units; a missing one is logged by the loader and leaves the reflections unavailable. Needs a live
 *  GL context. */
void WorldRendererReflections_LoadShader(WorldRenderer* self);

/**
 * @brief Takes the scene's new size, after WorldRendererScene_EnsureTargets made the scene target, and while the
 *        player's quality is not off gives the scene target its reflection attachments (in the HDR tier with a samplable
 *        depth only; others take none) and makes the block image and the pass's target (the scene's size over the
 *        quality's divisor). A failure is logged once and leaves the reflections unavailable for this size.
 * @param self The renderer (its scene target made).
 * @param width The scene's width, in pixels.
 * @param height The scene's height, in pixels.
 */
void WorldRendererReflections_CreateTargets(WorldRenderer* self, int width, int height);

/** @brief Takes the attachments off the scene target and unloads them, the block image and the pass's target (before the
 *  scene target goes). */
void WorldRendererReflections_ReleaseTargets(WorldRenderer* self);

/** @brief Follows the player's quality each render, after the scene's targets: off releases the attachments and the
 *  targets, another quality makes them (once per scene size after a failure) and makes the pass's target again when its
 *  divisor changed. */
void WorldRendererReflections_EnsureTarget(WorldRenderer* self);

/** @brief Whether the reflections can run at all, whatever the quality: their programs (the pass's and the blocks'), PBR
 *  lighting, the terrain's shader, a scene target in the HDR tier with a samplable depth, and attachments and targets
 *  that could be made for its size. */
bool WorldRendererReflections_IsAvailable(const WorldRenderer* self);

/**
 * @brief Decides whether the pass runs this frame and with what (the frame's AreReflectionsActive, ReflectionMarch,
 *        MirroringObjectCount and AreWetSurfacesMirroring): available with its attachments and targets made, switched
 *        on, the player's quality not off, no debug view drawn over the surfaces' colours (interior lighting's sky view,
 *        the shadow levels' view, the rain's views: the lit shaders then mirror the sky themselves under them), and
 *        something in view mirrors (a model or a copies' component with has_reflections in the camera's lists, or wet
 *        surfaces while the rain is on). Needs the frame's visibility, its rain and the camera's interior.
 */
void WorldRendererReflections_Decide(WorldRenderer* self, WorldRenderFrame* frame);

/** @brief Clears the scene target's reflection attachments to "no reflection" while the pass runs this frame; call inside
 *  the scene target's texture mode, before its colour is cleared (it clears the depth too). */
void WorldRendererReflections_ClearData(const WorldRenderFrame* frame);

/** @brief Starts the opaque world's draws into the attachments while the pass runs (every draw until
 *  WorldRendererReflections_EndSurfaces must write them: the lit programs with common/reflection_output). */
void WorldRendererReflections_BeginSurfaces(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Ends the opaque world's draws into the attachments: the draws after draw the colour alone. */
void WorldRendererReflections_EndSurfaces(WorldRenderer* self);

/** @brief Pauses the opaque world's writes into the attachments for a draw that does not write them (a debug wireframe:
 *  it would leave undefined values there); WorldRendererReflections_ResumeSurfaces resumes them. Nothing while they are
 *  not being written. */
void WorldRendererReflections_PauseSurfaces(WorldRenderer* self);

/** @brief Resumes the writes WorldRendererReflections_PauseSurfaces paused. */
void WorldRendererReflections_ResumeSurfaces(WorldRenderer* self);

/**
 * @brief Runs the reflection pass while the frame has it: the block image from the scene's depth, then every mirroring
 *        pixel's ray marched over the depth block by block into the pass's target (or its debug view), then added to the
 *        scene target's colour (the debug view laid over it). Call after the opaque world (and the water), outside the
 *        scene target's texture mode.
 */
void WorldRendererReflections_Render(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief The status's "reflections" feature and the reflections' fields, from the last render. */
void WorldRendererReflections_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Logs one line about the reflections (with the status, WorldRenderer_LogStatus). */
void WorldRendererReflections_LogStatus(const WorldRenderer* self);


// WorldRendererWater.c
/** @brief Starts the water's state: no programs, no copies, a drift at nothing. */
void WorldRendererWater_Construct(WorldRenderer* self);

/**
 * @brief Loads the water's programs (world_water, then its instanced twin world_water_instanced), each a lit program
 *        (WorldRendererLighting_LoadPbrProgram) with the water's uniforms, its samplers pointed at their units. Without
 *        the rigid one no water is drawn (logged once by the loader). Needs a live GL context; before the instanced
 *        twins' load (WorldRendererInstances_LoadShaders prepares the water twin's instancing).
 */
void WorldRendererWater_LoadShaders(WorldRenderer* self);

/**
 * @brief Notes the scene's new size: the copies of an old size are released, and made again when water first needs them
 *        (WorldRendererWater_PrepareFrame).
 */
void WorldRendererWater_CreateTargets(WorldRenderer* self, int width, int height);

/** @brief Releases the copies. */
void WorldRendererWater_ReleaseTargets(WorldRenderer* self);

/** @brief Whether water can be drawn at all: the rigid program loaded. */
bool WorldRendererWater_IsAvailable(const WorldRenderer* self);

/**
 * @brief The frame's water: runs the drift by @p deltaSeconds with the environment's wind, counts the models and the
 *        copies' cells in view with water slots (and those that mirror their surroundings), and decides whether the pass
 *        runs and whether it sees what lies under the water. Needs the frame's visibility; before the reflections'
 *        decision (WorldRendererReflections_Decide counts the water that mirrors).
 */
void WorldRendererWater_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame, double deltaSeconds);

/**
 * @brief The material a water slot's mesh draws with in the water pass: no maps (DrawMesh binds none), @p program's
 *        pass shader; the water material's uniforms (its maps' frames for the frame, its look) uploaded into @p program
 *        unless it holds them already this frame, and its maps bound on the water's units.
 * @param self The renderer.
 * @param program The water program the mesh draws with (the rigid or the instanced one).
 * @param model The cached model (its world model and variant pick the water material).
 * @param materialIndex The mesh's material index: one of the world model's water slots.
 * @param outMaps [out] The material's maps (WORLD_RENDERER_MATERIAL_MAP_COUNT of them, zeroed).
 * @returns The material; its shader's id is 0 when the slot has no water material (nothing should be drawn).
 */
Material WorldRendererWater_PrepareMaterial(WorldRenderer* self, const WorldRendererWaterProgram* program,
    const WorldRenderModel* model, int materialIndex, MaterialMap* outMaps);

/**
 * @brief The water pass, inside the scene pass's texture mode and 3D mode, after the opaque world (its reflection
 *        attachments still written while the reflections run): copies the scene's colour and depth, binds them, and draws
 *        the camera's models' and copies' water slots over the opaque world (dithered by their fades, both LODs of a
 *        cross-fade), each with its forward lights and draw place. Nothing while the frame has no water.
 */
void WorldRendererWater_Render(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief The status's "water" feature and the water's fields, from the last render. */
void WorldRendererWater_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Logs one line about the water (with the status, WorldRenderer_LogStatus). */
void WorldRendererWater_LogStatus(const WorldRenderer* self);


// WorldRendererRain.c
/** @brief Starts the rain's state: no map, no meshes, empty texture slots, no drift; switched on, no debug view. */
void WorldRendererRain_Construct(WorldRenderer* self);

/** @brief Releases the rain's texture slots (the GL resources go with WorldRendererRain_Unload). */
void WorldRendererRain_Deconstruct(WorldRenderer* self);

/**
 * @brief Loads the rain's programs (each optional: the streaks, the splashes, the bolt), builds the streaks' and the
 *        splashes' meshes and loads the rain map's GL functions (the map itself is made at the first frame that needs
 *        it). Needs a live GL context; each failure is logged once and switches its part off.
 */
void WorldRendererRain_Load(WorldRenderer* self);

/** @brief Releases the rain map and the meshes (GL). */
void WorldRendererRain_Unload(WorldRenderer* self);

/**
 * @brief Points a lit program's rain samplers at their fixed units and caches its rain locations (common/rain,
 *        common/puddles).
 * @param shader The program.
 * @param outLocations [out] Its rain locations.
 */
void WorldRendererRain_PrepareLitShader(Shader shader, WorldRendererRainLocations* outLocations);

/**
 * @brief The frame's rain: syncs the textures the world names, takes its values from the environment, advances the
 *        drift by the frame's real seconds, decides what is drawn (the streaks, the splashes, the bolt) and the rain map's
 *        box (snapped to its texels; WorldRain_GetMapShape). Before the frame's visibility, which lists the map's
 *        casters in its box. Needs the frame's weather (the flash) and its origin.
 * @param self The renderer.
 * @param frame The frame; its rain fields are set.
 * @param deltaSeconds The frame's real seconds.
 */
void WorldRendererRain_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame, double deltaSeconds);

/** @brief Draws the rain map while the frame needs it: the depth of what keeps the rain out in its box, both faces,
 *  nearest first, with depth clamping. Opens and closes its own texture pass. */
void WorldRendererRain_RenderMap(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Uploads the frame's rain to a lit program: the map, the puddles, the sky mirrored in water, the bolt's light,
 *  the debug view. */
void WorldRendererRain_UploadLitFrame(const WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererRainLocations* locations);

/** @brief Binds the rain map, the puddle mask and the sky view on their fixed units, with the lit passes' frame
 *  textures. */
void WorldRendererRain_BindFrameTextures(const WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Draws the rain in the scene pass, after the sprites: the bolt, the splashes and the streaks (blended, depth
 *  tested, writing no depth), each while the frame draws it. Inside the scene's 3D mode. */
void WorldRendererRain_DrawScene(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief The status's "rain" and "rain map" features and the rain's fields, from the last render. */
void WorldRendererRain_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame);

/** @brief Logs one line about the rain (with the status, WorldRenderer_LogStatus). */
void WorldRendererRain_LogStatus(const WorldRenderer* self);
