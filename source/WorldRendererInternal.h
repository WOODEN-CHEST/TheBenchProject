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
#include "WorldEnvironment.h"
#include "WorldModelComponent.h"
#include "WorldSpriteComponent.h"
#include "NightVision.h"
#include "SpriteTransform.h"
#include "WorldVisibility.h"
#include "raylib/raylib.h"
// rlgl supplies RL_MAX_SHADER_LOCATIONS and the scene near/far cull distances.
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"

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
 *   WorldRendererSky.c       the atmospheric sky with the moon, the Milky Way and light pollution's skyglow
 *   WorldRendererStars.c     the star field (the seeded catalogue as one-pixel quads)
 *   WorldRendererPost.c      outlines, bloom, sun shafts and the tonemap
 *   WorldRendererCrisp.c     the crisp overlay for OmitPixelation objects
 * plus the per-object render cache (WorldRenderCache.h) and each frame's visibility (WorldVisibility.h: what each pass
 * draws, with every entry's fade). Each pass keeps its shader, uniform locations, targets and toggles in its own state
 * struct below.
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
/** Near/far planes the scene 3D pass uses (Raylib's global cull distances, which BeginMode3D applies); the
 *  frame context rebuilds the same projection for the passes that reconstruct view positions from depth. */
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
    WorldRendererTextureUnit_ShadowNear = 12,       // sampler2D shadowMap (near sun cascade)
    WorldRendererTextureUnit_ShadowFar = 13,        // sampler2D shadowMapFar (far sun cascade)
    WorldRendererTextureUnit_PointShadowCube = 14,  // samplerCube pointShadowCube
    WorldRendererTextureUnit_AmbientOcclusion = 15, // sampler2D ambientOcclusionMap
} WorldRendererTextureUnit;

_Static_assert(WorldRendererTextureUnit_ShadowNear >= WORLD_RENDERER_MATERIAL_MAP_COUNT,
    "renderer-owned texture units must sit above raylib's material map units");

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

/** @brief One of the sky's directional lights (the sun or the moon) as the PBR shader takes it. */
typedef struct WorldRendererDirectionalLightStruct
{
    /** @brief Unit direction TO the light. */
    Vector3 Direction;
    /** @brief Linear colour. */
    float Color[3];
    /** @brief Intensity on that colour, with daylight (the sun) or phase, height and night (the moon) already in; 0 =
     *  the light is off. */
    float Intensity;
} WorldRendererDirectionalLight;

/** @brief One shadow cascade's light matrices and box for a frame (WorldRendererShadows_PrepareFrame). */
typedef struct WorldRendererCascadeFrameStruct
{
    /** @brief The light's view (looking at the camera's position along the shadow light's direction) and its
     *  orthographic projection. */
    Matrix View;
    Matrix Projection;
    /** @brief World -> light clip (raymath order: MatrixMultiply(View, Projection)): the lit shaders' lightVP or
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
    /** @brief The camera, and the raylib camera made from it. */
    const GameCamera* Camera;
    Camera3D RayCamera;
    /** @brief The scene's view and projection matrices (what BeginMode3D builds for the scene target), the
     *  projection's inverse, and the inverse of view x projection. */
    Matrix View;
    Matrix Projection;
    Matrix InvProjection;
    Matrix InvViewProjection;
    /** @brief View x projection (raymath order: MatrixMultiply(View, Projection)). */
    Matrix ViewProjection;
    /** @brief The camera's frustum (from ViewProjection): what the camera's passes (scene, G-buffer, crisp) cull
     *  against. */
    WorldFrustum CameraFrustum;
    /** @brief The sky's sun: unit direction to it (the sky, the fog, the sun shafts and the outlines' creases use it),
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
    /** @brief Night sky values (WorldRendererSky_PrepareFrame): the faintest magnitude visible overhead (light
     *  pollution, the moonlit sky and twilight), the Milky Way's linear radiance per white texel (brightness, light
     *  pollution and twilight included), and light pollution's skyglow at the horizon (linear; 0 by day), which the sky
     *  and the PBR fog both add. */
    float LimitingMagnitude;
    float MilkyWayRadiance;
    float SkyglowRadiance[3];
    /** @brief The moon's light on the atmosphere, relative to the sun's (the moonlit sky the sky pass and the PBR fog
     *  add); 0 by day, without a moon, or while the moon is far below the horizon. */
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

    /** @brief The sky's two directional lights (directions and intensities from WorldEnvironment_GetSkyLights, linear
     *  colours from WorldRendererLighting_PrepareFrame), shared by the scene and crisp passes. */
    WorldRendererDirectionalLight SunLight;
    WorldRendererDirectionalLight MoonLight;
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
    /** @brief The cube shadow's light: its position, and the far range its packed distances are normalized by (its reach,
     *  at least twice the faces' near plane), which is also the reach its casters must touch; valid while
     *  IsPointShadowActive. */
    Vector3 PointShadowPosition;
    float PointShadowFar;
    /** @brief Lighting shared by the scene and crisp passes (computed once per render): linear ambient colour, the
     *  scene luminance estimate eye adaptation starts from, the fog density, and the shadow strength. */
    float AmbientColor[3];
    float BaseSceneLuminance;
    float FogDensity;
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
    /** @brief The eye's night vision this frame, which the sky and PBR shaders apply: how strongly (the renderer toggle
     *  x the world's NightVisionStrength; 0 = off), the field luminance it compares with (BaseSceneLuminance: the sky's
     *  light, without nearby lamps), the rods' grey (linear, luminance 1, from the world's NightVisionBlueShift), and
     *  whether it changes anything (on, with the field below the full-colour luminance: dusk and night). */
    float NightVisionStrength;
    float NightVisionFieldLuminance;
    float NightVisionTint[3];
    bool IsNightVisionActive;
} WorldRenderFrame;

/**
 * @brief The dither uniforms of a geometry shader (shaders/common/dither.glsl): its fade, whether it is inverted, and the
 *        pass's pixels per scene pixel. -1 for a uniform the shader lacks.
 *
 * Filled by WorldRendererCommon_PrepareDither, which also uploads the defaults (solid, not inverted, scale 1).
 */
typedef struct WorldRendererDitherLocationsStruct
{
    int _locFade;
    int _locInvert;
    int _locPixelScale;
} WorldRendererDitherLocations;

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
 * @brief The uniform locations of a shader that includes shaders/common/lighting.glsl (the PBR and the sprite
 *        shaders): the frame's lights, shadows, AO, fog and night vision, and the per-object forward point lights.
 *
 * Filled by WorldRendererLighting_PrepareLitShader; -1 for a uniform the shader lacks (or optimized out).
 */
typedef struct WorldRendererLitLocationsStruct
{
    /** @brief How many point lights the shader's uniform arrays hold, capped at WORLD_MAX_FORWARD_LIGHTS
     *  (checked at load; uploads never exceed it). */
    int _maxPointLights;
    /** @brief The camera, the two directional lights (the sun's direction is also the fog sky's), the ambient. */
    int _locViewPos;
    int _locSunDirection;
    int _locSunColor;
    int _locSunIntensity;
    int _locMoonDirection;
    int _locMoonColor;
    int _locMoonIntensity;
    int _locAmbientColor;
    int _locAmbientIntensity;
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
    /** @brief The distance fog: it fades distant geometry into the atmospheric sky colour, so it needs the sky
     *  parameters the sky pass uses plus the fog density. */
    int _locFogDensity;
    int _locFogTurbidity;
    int _locFogSkyTint;
    int _locFogSunIntensity;
    int _locFogSkyglow;
    int _locFogMoonIntensity;
    /** @brief The night vision: the field luminance, the calibration (uploaded once at load), the strength and the rods'
     *  grey. */
    int _locNightVisionFieldLuminance;
    int _locNightVisionHalfLuminance;
    int _locNightVisionSteepness;
    int _locNightVisionStrength;
    int _locNightVisionTint;
} WorldRendererLitLocations;

/** @brief The PBR shader that shades model objects, its uniform locations, and this render's forward lights. */
typedef struct WorldRendererPbrStateStruct
{
    /** @brief The PBR shader asset (held under the shader user); NULL = unavailable (models draw unlit). */
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
    /** @brief This render's forward-light candidates (WorldCullingLight), which every lit shader (models and sprites)
     *  picks each object's lights from: every light of the render cache except the cube-shadow light, which is shaded on
     *  its own. */
    GenericBuffer _forwardLights;
    /** @brief The candidates' linear radiance (WorldRendererRadiance), parallel to _forwardLights. */
    GenericBuffer _forwardRadiances;
} WorldRendererPbrState;

/** @brief HDR eye adaptation (auto-exposure) state. */
typedef struct WorldRendererExposureStateStruct
{
    /** @brief The eye's currently adapted luminance (drives auto-exposure); eased toward the scene luminance. */
    float _adaptedLuminance;
    /** @brief false until the first adaptation update snaps _adaptedLuminance to the scene (no first-frame flash). */
    bool _isInitialized;
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
    int _locCameraPos;
    int _locSunDirection;
    int _locSunColor;
    int _locSunIntensity;
    int _locSunSize;
    int _locTurbidity;
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
} WorldRendererSkyState;

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
    /** @brief The lit sprite shader (world_sprite: lights, shadows, fog, night vision, alpha modes); NULL = sprites are
     *  not drawn at all. */
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
} WorldRendererCascadeState;

/** @brief The cube shadow of the shadow-casting point light. */
typedef struct WorldRendererPointShadowStateStruct
{
    /** @brief The cube-depth shader; NULL = no point-light shadow. Cached uniform locations: the shadow light's
     *  world position and far range, and the dither. */
    GameShader* _shader;
    int _locLightPos;
    int _locLightFar;
    WorldRendererDitherLocations _dither;
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
    /** @brief Cached world_normal outline-flag uniform location (-1 when absent). */
    int _locOutlineFlag;
    /** @brief The two G-buffer shaders' dither uniforms: their fade stays at the default 0 (the G-buffer draws a fading
     *  object solid below WORLD_VISIBILITY_GBUFFER_FADE_LIMIT and skips it above). */
    WorldRendererDitherLocations _dither;
    WorldRendererDitherLocations _spriteDither;
    /** @brief The G-buffer shader for cutout sprites (alpha-tested at each sprite's cutoff); NULL = sprites get no
     *  outlines and no AO of their own. */
    GameShader* _spriteShader;
    /** @brief Cached world_normal_sprite uniform locations (-1 when absent): the outline flag, the frame's area and the
     *  cutoff. */
    int _spriteLocOutlineFlag;
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

/** @brief The crisp overlay (un-pixelated OmitPixelation objects composited over the pixelated frame). */
typedef struct WorldRendererCrispStateStruct
{
    /** @brief The crisp-composite shader; NULL = unavailable (flagged objects render pixelated instead). */
    GameShader* _compositeShader;
    /** @brief Cached crisp-composite uniform locations (-1 when absent). */
    int _locCrispDepth;
    int _locSceneDepth;
    int _locExposure;
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
    /** @brief Whether the final pixelation pass is applied. */
    bool _pixelationEnabled;
    /** @brief Set once the shaders' load has been attempted (success or failure), so it is tried only once. */
    bool _shadersLoadAttempted;
    /** @brief Master toggle for AO + outlines; when false the scene blits straight to the tonemap. On by default. */
    bool _postfxEnabled;
    /** @brief Toggle for the eye's night vision (A/B; on by default); the world's NightVisionStrength scales it. */
    bool _isNightVisionEnabled;
    /** @brief Toggle for sprite lighting (A/B; on by default): off draws every sprite unlit at its full colours, as
     *  before sprites were lit. */
    bool _isSpriteLightingEnabled;
    /** @brief The night vision's calibration on the game's light scale (NightVision_GetCalibration), computed once. */
    NightVisionCalibration _nightVision;
    /** @brief The per-object render cache and this render's model/sprite/light lists. */
    WorldRenderCache _cache;
    /** @brief This render's visibility: per pass the cache entries it draws (the camera's frustum, each cascade's box,
     *  the cube's reach, the draw distances and opacities), per entry its fades. Built right after the cache's sync. */
    WorldVisibility _visibility;
    /** @brief Toggle for the camera's frustum culling and the cascades' box culling (A/B; on by default). Off draws
     *  everything within its draw distance, as before culling existed. */
    bool _isCullingEnabled;
    /** @brief What the most recent render did (WorldRenderer_GetStatus). */
    WorldRendererStatus _status;
    /** @brief Set once the status has been logged after the first render. */
    bool _isStatusReported;
    /** @brief The render clock behind WorldRenderFrame.TimeSeconds: seconds of render time, wrapped every hour. */
    double _renderTimeSeconds;

    /** @brief Per-pass state (shader, uniform locations, targets, toggles). */
    WorldRendererSceneState _scene;
    WorldRendererPbrState _pbr;
    WorldRendererExposureState _exposure;
    WorldRendererTonemapState _tonemap;
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

/**
 * @brief Caches a geometry shader's dither uniforms (shaders/common/dither.glsl) and uploads their defaults: solid (fade
 *        0), not inverted, a pixel scale of 1. Call once at load; passes then change only what differs.
 * @param shader The shader (its program).
 * @param outLocations [out] Receives the locations (-1 for a uniform the shader lacks).
 */
void WorldRendererCommon_PrepareDither(Shader shader, WorldRendererDitherLocations* outLocations);

/**
 * @brief Uploads a draw's dither: its fade (0 solid .. 1 gone) and whether it uses the mirrored pattern.
 * @param shader The shader (its program).
 * @param locations Its dither locations (WorldRendererCommon_PrepareDither).
 * @param fade The draw's fade.
 * @param isInverted Whether the draw uses the mirrored pattern (T11's incoming LOD, at 1 - the outgoing's fade: the two
 *        keep complementary pixels); false otherwise.
 */
void WorldRendererCommon_SetDither(Shader shader, const WorldRendererDitherLocations* locations, float fade,
    bool isInverted);

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
 * @brief Tries to create an HDR (RGBA16F) framebuffer with a depth attachment.
 *
 * The depth is a samplable texture when @p samplableDepth is true, or a renderbuffer otherwise (depth testing
 * is identical either way).
 * @returns The target, or a zero target (id 0) if the GPU cannot make the combination complete.
 */
RenderTexture2D WorldRendererCommon_TryCreateHdrTarget(int width, int height, bool samplableDepth);

/**
 * @brief Creates a colour-only HDR (RGBA16F) framebuffer, for passes that write every pixel without depth.
 * @returns The target, or a zero target (id 0) if it could not be made complete.
 */
RenderTexture2D WorldRendererCommon_CreateColorTarget(int width, int height);

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
 * @brief Draws one cached model mesh by mesh, each mesh with a material built on the stack (the asset's maps
 *        copied, @p passShader, the object's tint applied to the copy), so nothing is written to the asset. The caller
 *        uploads the draw's dither to @p passShader first.
 *
 * Must run inside a 3D mode (BeginMode3D or explicit rlgl matrices).
 * @param self The renderer.
 * @param model The cached model; must have an asset.
 * @param passShader The shader to draw with, or NULL to keep each material's own shader.
 * @param isLit Whether this is the lit (PBR) pass: then the object's forward point lights and each material's
 *        PBR factors are uploaded before its meshes.
 */
void WorldRendererGeometry_DrawModel(WorldRenderer* self, const WorldRenderModel* model, const Shader* passShader,
    bool isLit);

/** @brief Draws the camera's models (the visibility's camera list) that pass @p filter, each with its fade dithered when
 *  @p isLit (the PBR pass; see WorldRendererGeometry_DrawModel). */
void WorldRendererGeometry_DrawModels(WorldRenderer* self, const Shader* passShader, bool isLit,
    WorldRendererPixelFilter filter);

/** @brief Draws a small wireframe sphere at each light, tinted by its colour (a debug aid). Inside BeginMode3D. */
void WorldRendererGeometry_DrawLightGizmos(WorldRenderer* self);


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
 * @brief Computes each cached sprite's matrices for this frame (facing the camera; facing the cascades' light and the
 *        cube shadow's light while those run) and the draw order of the blended and additive sprites the camera's
 *        passes draw, back to front.
 *
 * Needs the frame's camera, IsCascadeShadowActive / ShadowDirection, IsPointShadowActive / PointShadowPosition and the
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
 * @brief Loads the PBR shader, caches its uniform locations, builds the renderer's location table and pass
 *        shader, points its samplers at their fixed units, and checks how many point lights it supports.
 *
 * Without it models draw unlit with Raylib's default shader.
 */
void WorldRendererLighting_LoadShader(WorldRenderer* self);

/**
 * @brief Prepares a shader that includes shaders/common/lighting.glsl: caches its frame-lighting uniform locations,
 *        uploads the constants (the shadow bias and texel size, the night vision's calibration), points its shadow and
 *        AO samplers at their fixed units, and checks how many point lights its arrays hold (logging once when fewer
 *        than WORLD_MAX_FORWARD_LIGHTS).
 * @param self The renderer.
 * @param shader The shader (its program).
 * @param assetName Its asset name, for the log line.
 * @param outLocations [out] Receives the locations.
 */
void WorldRendererLighting_PrepareLitShader(WorldRenderer* self, Shader shader, const unsigned char* assetName,
    WorldRendererLitLocations* outLocations);

/**
 * @brief Computes the frame's lighting (the sun's and the moon's colours, the ambient with its moonlit share, fog,
 *        shadow strength, scene luminance) into @p frame and builds the forward-light candidates (every light except
 *        the frame's cube-shadow light).
 *
 * Needs the frame's sky lights (SunLight and MoonLight directions and intensities, Daylight), IsPointShadowActive and
 * ShadowLightIndex.
 */
void WorldRendererLighting_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame);

/**
 * @brief Uploads the frame's lighting uniforms to every lit shader the renderer has (the PBR shader and the sprite
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
 * @brief Reach-culls the frame's forward lights against a world bounding sphere and uploads the strongest few to a lit
 *        shader, for the object drawn next.
 * @param self The renderer.
 * @param shader The lit shader (its program).
 * @param locations Its locations (WorldRendererLighting_PrepareLitShader).
 * @param center The object's bounding sphere's centre.
 * @param radius Its radius.
 */
void WorldRendererLighting_UploadObjectLights(WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, Vector3 center, float radius);

/**
 * @brief Uploads one material's PBR scalar factors and map-presence flags, for its meshes drawn next.
 *
 * DrawMesh uploads colDiffuse and binds the maps itself, but not these.
 */
void WorldRendererLighting_UploadMaterial(WorldRenderer* self, const Material* material);

/**
 * @brief Eases the adapted luminance toward the frame's scene luminance (plus nearby point lights) and returns
 *        the tonemap exposure.
 * @param self The renderer.
 * @param frame The frame (BaseSceneLuminance must be computed).
 * @param deltaSeconds Real seconds since the previous render; 0, negative or non-finite holds the adaptation.
 * @returns The exposure (always finite and within the adaptation clamp).
 */
float WorldRendererLighting_UpdateExposure(WorldRenderer* self, const WorldRenderFrame* frame, float deltaSeconds);


// WorldRendererShadows.c
/** @brief Loads the depth and cube-depth shaders (and their alpha-tested sprite variants) and creates the cascade maps
 *  and the shadow cube. */
void WorldRendererShadows_LoadShaders(WorldRenderer* self);

/** @brief Unloads the cascade maps and the shadow cube. Needs a live GL context. */
void WorldRendererShadows_Unload(WorldRenderer* self);

/**
 * @brief Picks the light that casts the cube shadow: the CastsShadows light with positive intensity and reach
 *        nearest @p cameraPosition.
 * @returns Its index in the render cache's lights, or SIZE_MAX when no light qualifies.
 */
size_t WorldRendererShadows_SelectPointLight(const WorldRenderer* self, Vector3 cameraPosition);

/**
 * @brief Fills the frame's shadow values the passes and the visibility share: both cascades' light matrices and boxes
 *        (while IsCascadeShadowActive; from frame->ShadowDirection, centred on the camera) and the cube shadow's light
 *        position and far range (while IsPointShadowActive). Call after the shadow decisions, before the visibility.
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
 * @brief Computes the frame's night sky values: the limiting magnitude (light pollution, the moonlit sky, twilight),
 *        the Milky Way's radiance, the skyglow, the moon's light on the atmosphere and its disc.
 *
 * Needs frame->Environment, the sun's and the moon's directions, Daylight and MoonIllumination.
 */
void WorldRendererSky_PrepareFrame(WorldRenderFrame* frame);

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

/** @brief Bright-passes @p source into the bloom buffers and blurs it, leaving the bloom in the A buffer. */
void WorldRendererPost_RenderBloom(WorldRenderer* self, RenderTexture2D source);

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
