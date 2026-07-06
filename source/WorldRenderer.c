#include <math.h>
#include "WorldRenderer.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelObject.h"
#include "WorldSpriteObject.h"
#include "SpriteAnimation.h"
#include "GameCamera.h"
#include "AssetManager.h"
#include "GameModel.h"
#include "GameShader.h"
#include "Config.h"
#include "Logger.h"
#include "Renderer.h"
#include "WorldLight.h"
#include "WorldLightCulling.h"
#include "raylib/raylib.h"
#include "raylib/raymath.h"
// rlgl is needed to build a floating-point (HDR) framebuffer; raylib's LoadRenderTexture is 8-bit only.
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Macros.
/** Number of grid lines to each side of the origin for the debug reference grid. */
#define DEBUG_GRID_SLICES 40
/** World-space spacing between debug grid lines. */
#define DEBUG_GRID_SPACING 1.0f
/** Pixel count on the LONGER window axis for the pixelation pass; the shorter axis is derived to keep
 *  pixels square. For a 16:9 window this yields the game's 640x360 pixel grid. */
#define PIXELATION_LONG_AXIS 640

/** Asset name of the world PBR (metallic/roughness) shader used to shade model objects. */
#define PBR_SHADER_ASSET_NAME ((const unsigned char*)u8"world_pbr")
/** Asset name of the tonemap post-pass shader (maps the linear-HDR scene to displayable sRGB). */
#define TONEMAP_SHADER_ASSET_NAME ((const unsigned char*)u8"world_tonemap")
/** Asset name of the atmospheric sky shader (full-screen Rayleigh/Mie sky + sun + stars). */
#define SKY_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sky")
/** Bookkeeping value Raylib uses for a RenderTexture's depth attachment format (24-bit depth renderbuffer). */
#define SCENE_DEPTH_FORMAT 19
/** Default surface metallic value (dielectric) until per-material PBR maps exist. */
#define PBR_DEFAULT_METALLIC 0.0f
/** Default surface roughness until per-material PBR maps exist. */
#define PBR_DEFAULT_ROUGHNESS 0.5f
/** Default ambient occlusion (none) until per-material occlusion maps exist. */
#define PBR_DEFAULT_AO 1.0f
/** sRGB gamma used to convert stored 8-bit colours to/from linear light. */
#define SRGB_GAMMA 2.2f

/** Asset name of the depth-only shader used for the sun shadow-map pass. */
#define DEPTH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_depth")
/** Resolution (square) of the sun shadow map, in texels. */
#define SHADOW_MAP_SIZE 2048
/** World-space size (width/height) of the sun's orthographic shadow frustum, centred on the camera. Larger
 *  covers more of the scene (shadows persist as you move) at the cost of shadow-map resolution per unit. */
#define SHADOW_ORTHO_SIZE 60.0f
/** Distance the shadow light camera is placed from its focus, along the sun direction. */
#define SHADOW_DISTANCE 80.0f
/** Near plane of the shadow orthographic frustum. */
#define SHADOW_NEAR 1.0f
/** Far plane of the shadow orthographic frustum (brackets the scene around the focus). */
#define SHADOW_FAR (SHADOW_DISTANCE * 2.0f)
/** Base shadow depth bias (normalized light-clip depth) to combat acne; slope-scaled in the shader. Tuned
 *  for the SHADOW_NEAR..SHADOW_FAR range: ~0.0009 * (far-near) is the world-space offset (a few cm here). */
#define SHADOW_BIAS 0.0009f
/** Minimum sun elevation (sunDir.y) for shadows to be cast; below this the sun is too low / set. */
#define SHADOW_MIN_SUN_ELEVATION 0.05f
/** Texture unit the shadow map is bound to for the PBR shader (kept clear of the material map slots 0-2). */
#define SHADOW_TEXTURE_SLOT 10

/** Capacity of the per-asset local-bounds cache used for point-light reach culling (distinct model assets).
 *  Caching avoids re-scanning a model's mesh vertices every frame just to get its bounding sphere. */
#define BOUNDS_CACHE_CAPACITY 256

/** ===== NIGHT DARKNESS KNOB (1 of 2) =====
 *  Dim cool night-ambient floor (LINEAR RGB): the darkest the sky-derived ambient is allowed to get at night.
 *  Kept just above 0 (a faint blue, like moonlight) so night is not pure black and the eye adaptation has a
 *  little light to lift. LOWER these to make the night DARKER (and less tinted); raise them to brighten it.
 *  Works together with ADAPT_MAX_EXPOSURE below — the perceived night brightness is roughly this floor's
 *  luminance times that exposure cap. */
#define NIGHT_AMBIENT_R 0.004f
#define NIGHT_AMBIENT_G 0.005f
#define NIGHT_AMBIENT_B 0.011f
/** Overall scale on the daytime ambient skylight (before the night floor). Tempers the fill-light magnitude so
 *  daytime sits at a natural level; the day/night fade and the warm twilight tint are applied on top. */
#define AMBIENT_SKY_STRENGTH 0.85f
/** Daytime ambient fill colour (LINEAR RGB) — the blue skylight when the sun is well above the horizon. */
#define AMBIENT_DAY_R 0.15f
#define AMBIENT_DAY_G 0.35f
#define AMBIENT_DAY_B 0.75f
/** Twilight ambient fill colour (LINEAR RGB) — the warm sunrise/sunset tint the fill blends toward while the
 *  sun is near the horizon. */
#define AMBIENT_SUNSET_R 0.80f
#define AMBIENT_SUNSET_G 0.28f
#define AMBIENT_SUNSET_B 0.08f
/** How near the horizon (in sun-elevation units, where 0 = on the horizon and 1 = zenith) the ambient warms
 *  toward the sunset colour: the warm tint is a Gaussian of the sun's elevation with this as its width, so a
 *  SMALLER value keeps the fill blue until the sun is closer to setting, a LARGER value warms it earlier.
 *  This is the "orange comes in too early / too late" knob. The warm tint is additionally gated by the daylight
 *  factor so it can never linger once the sun has set. */
#define AMBIENT_SUNSET_BAND 0.15f
/** How much the (directional) sun counts toward the scene-illumination estimate driving eye adaptation. */
#define SUN_ADAPT_WEIGHT 0.5f
/** Base exponential fog density at world+config fog strength 1.0 (per world unit). Distant geometry fades into
 *  the atmospheric sky colour; the world and config fog-strength multipliers scale this. */
#define BASE_FOG_DENSITY 0.008f

// ---- Eye adaptation (HDR auto-exposure) ----
/** Scene illumination that maps to exposure 1.0 (roughly full daylight); brighter dims, darker brightens. */
#define ADAPT_REFERENCE_LUMINANCE 0.80f
/** Exposure clamp: never darker than this (so a bright light cannot black the world out)... */
#define ADAPT_MIN_EXPOSURE 0.5f
/** ===== NIGHT DARKNESS KNOB (2 of 2) =====
 *  ...and never brighter than this. This bounds how far the auto-exposure lifts a dark night, so it is the
 *  dominant control of how bright the night LOOKS: the eye adaptation would otherwise brighten the dim night
 *  ambient all the way back up. LOWER this to make the night DARKER; raise it to brighten the night. (Was 40;
 *  lowered so night reads as night.) Pairs with the NIGHT_AMBIENT_* floor above. */
#define ADAPT_MAX_EXPOSURE 24.0f
/** Floor on the estimated scene luminance, so exposure never divides by ~0. */
#define ADAPT_MIN_LUMINANCE 0.02f
/** Base adaptation rate (1/seconds); deliberately slow. Higher = quicker adaptation overall. */
#define ADAPT_BASE_SPEED 0.6f
/** Extra rate per unit of |brightness change|, so large jumps adapt faster than gentle drifts. */
#define ADAPT_DELTA_GAIN 0.8f
/** Multiplier applied when BRIGHTENING (target > current): light adaptation is quicker than dark adaptation,
 *  so going from a dark night to facing a light snaps down fast, while dark-adapting stays slow. */
#define ADAPT_BRIGHTEN_MULTIPLIER 5.0f
/** Clamp on a single frame's delta time feeding adaptation, so one long stall cannot jump the exposure. */
#define ADAPT_MAX_DELTA_SECONDS 0.1f
/** Per-point-light weight in the adaptation estimate (how strongly being near a light lowers exposure). */
#define POINT_ADAPT_WEIGHT 0.5f

/** Asset name of the post-process shader (screen-space AO + hand-drawn outlines) run before the tonemap. */
#define POSTFX_SHADER_ASSET_NAME ((const unsigned char*)u8"world_postfx")
/** Asset name of the normal/mask G-buffer shader: writes a view-space normal (RGB) + surface/outline flag (A)
 *  that the postfx pass edge-detects for outlines. Replaces the old flag-only object mask. */
#define NORMAL_SHADER_ASSET_NAME ((const unsigned char*)u8"world_normal")
/** Built-in outline strength (multiplied by nothing config-side per spec: outlines are code-configurable).
 *  0..1; scales how strongly the depth/normal edge detection recolours silhouettes and creases. */
#define OUTLINE_BASE_STRENGTH 1.0f
/** Near/far planes the scene 3D pass uses (Raylib's global cull distances); the postfx pass rebuilds the same
 *  projection to reconstruct view positions from the depth buffer. */
#define SCENE_NEAR_PLANE ((double)RL_CULL_DISTANCE_NEAR)
#define SCENE_FAR_PLANE ((double)RL_CULL_DISTANCE_FAR)

/** Asset names of the two bloom shaders: a bright-pass/downsample prefilter and a separable Gaussian blur. */
#define BLOOM_PREFILTER_SHADER_ASSET_NAME ((const unsigned char*)u8"world_bloom_prefilter")
#define BLOOM_BLUR_SHADER_ASSET_NAME ((const unsigned char*)u8"world_bloom_blur")
/** Divisor on the scene resolution for the bloom buffers (2 = half res). Bloom is a soft glow, so a lower-res
 *  buffer is cheaper and looks the same; the blur taps rely on BILINEAR filtering of these buffers. */
#define BLOOM_RESOLUTION_DIVISOR 2
/** Number of horizontal+vertical blur iterations; more = a wider, softer glow (at more passes). */
#define BLOOM_BLUR_ITERATIONS 2
/** Linear-HDR brightness (max channel) above which a pixel starts to bloom, and the soft-knee width [0,1]. */
#define BLOOM_THRESHOLD 1.0f
#define BLOOM_SOFT_KNEE 0.5f
/** Base bloom intensity at world+config bloom strength 1.0 (how strongly the blurred bright-pass is added back
 *  in linear HDR); the world and config bloom-strength multipliers scale this. */
#define BLOOM_BASE_INTENSITY 0.75f

/** Asset name of the screen-space sun-shafts (god rays) shader. */
#define SUNSHAFT_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sunshafts")
/** Divisor on the scene resolution for the sun-shaft buffer (2 = half res). Shafts are soft, so a lower-res
 *  radial blur is cheaper and looks the same; the buffer is BILINEAR for a smooth upsample in the tonemap. */
#define SUNSHAFT_RESOLUTION_DIVISOR 2
/** Base sun-shaft intensity at world+config strength 1.0 (how strongly the shafts are added back in linear
 *  HDR); the world and config sunshaft-strength multipliers scale this. Kept low — shafts sample the very
 *  bright near-sun sky, so a little goes a long way. */
#define SUNSHAFT_BASE_INTENSITY 0.5f
/** Minimum sun elevation (sunDir.y) for shafts: below this the sun is too low/set and shafts are skipped, so
 *  they do not fire at night and stay gentle right at the horizon. */
#define SUNSHAFT_MIN_ELEVATION 0.05f
/** How far outside the [0,1] screen box the sun may sit and still cast shafts (rays from a just-off-screen sun
 *  still enter the frame); beyond this margin the pass is skipped. */
#define SUNSHAFT_SCREEN_MARGIN 0.35f

/** Asset name of the crisp-composite shader: composites the full-res, un-pixelated (OmitPixelation) objects
 *  over the pixelated frame, depth-tested against the scene so they are occluded correctly. */
#define CRISP_COMPOSITE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_crisp_composite")

/** Asset name of the sprite shader: draws 2D sprite billboards, linearizing their sRGB art into the HDR scene. */
#define SPRITE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_sprite")
/** Radius of the debug wireframe gizmo drawn at each light's position (only with the debug grid enabled). */
#define LIGHT_GIZMO_RADIUS 0.2f


// Types.
/* Which model objects a draw pass should include, by their OmitPixelation flag. The pixelated scene pass draws
 * only pixelated objects (when the crisp overlay is active), the crisp overlay pass draws only the flagged
 * ones, and the shadow pass draws all (everything casts shadows). */
typedef enum
{
    ModelPixelationFilter_All,
    ModelPixelationFilter_PixelatedOnly, // OmitPixelation == false
    ModelPixelationFilter_CrispOnly,     // OmitPixelation == true
} ModelPixelationFilter;

struct WorldRendererStruct
{
    /** Asset manager used to resolve object assets; borrowed. */
    AssetManager* _assetManager;
    /** Logger for diagnostics; borrowed, may be NULL. */
    Logger* _logger;
    /** Game config supplying the config-side post-effect multipliers; borrowed, may be NULL (defaults to 1x). */
    const GameConfig* _config;
    /** The renderer's asset user id; every asset it loads is held under this. */
    AssetUserID _assetUser;
    /** Whether the debug reference grid is drawn. */
    bool _drawDebugGrid;
    /** Whether the final pixelation pass is applied. */
    bool _pixelationEnabled;
    /** The PBR shader used to shade model objects; borrowed (held under _assetUser). NULL = unavailable. */
    GameShader* _pbrShader;
    /** The tonemap post-pass shader applied when blitting the HDR scene to the frame; borrowed. NULL = unavailable. */
    GameShader* _tonemapShader;
    /** Cached tonemap uniform locations (-1 when absent). */
    int _tonemapLocExposure;
    int _tonemapLocBloomTexture;
    int _tonemapLocBloomStrength;
    int _tonemapLocSunshaftTexture;
    int _tonemapLocSunshaftStrength;
    /** The two bloom shaders (bright-pass prefilter + separable blur); borrowed. NULL = bloom unavailable. */
    GameShader* _bloomPrefilterShader;
    GameShader* _bloomBlurShader;
    /** Cached bloom-prefilter uniform locations (-1 when absent). */
    int _bloomPrefilterLocResolution;
    int _bloomPrefilterLocThreshold;
    int _bloomPrefilterLocSoftKnee;
    int _bloomPrefilterLocDepthTexture;
    int _bloomPrefilterLocDepthMask;
    /** Cached bloom-blur uniform locations (-1 when absent). */
    int _bloomBlurLocResolution;
    int _bloomBlurLocDirection;
    /** Whether bloom is drawn (code toggle for A/B testing; on by default). Bloom also self-disables when the
     *  effective (world x config) bloom strength is 0. */
    bool _bloomEnabled;
    /** The sun-shafts (god rays) shader; borrowed. NULL = sun shafts unavailable. */
    GameShader* _sunshaftShader;
    /** Cached sun-shaft uniform locations (-1 when absent). */
    int _sunshaftLocResolution;
    int _sunshaftLocDepthTexture;
    int _sunshaftLocSunScreenPos;
    /** Whether sun shafts are drawn (code toggle for A/B testing; on by default). Also self-disable when the
     *  effective (world x config) strength is 0, the sun is below the horizon, or off-screen. */
    bool _sunshaftEnabled;
    /** The crisp-composite shader (draws OmitPixelation objects full-res over the pixelated frame); borrowed.
     *  NULL = unavailable, in which case flagged objects fall back to being drawn pixelated in the scene pass. */
    GameShader* _crispCompositeShader;
    /** Cached crisp-composite uniform locations (-1 when absent). */
    int _crispLocCrispDepth;
    int _crispLocSceneDepth;
    int _crispLocExposure;
    /** Whether the crisp overlay is drawn (code toggle for A/B testing; on by default). When off, OmitPixelation
     *  objects render pixelated with the rest of the world. */
    bool _crispEnabled;
    /** The sprite shader (draws 2D sprite billboards, sRGB->linear HDR); borrowed. NULL = sprites draw with
     *  Raylib's default shader (their sRGB art then lands in the linear buffer slightly wrong, but still shows). */
    GameShader* _spriteShader;
    /** Base scene-illumination luminance (ambient + sun) computed each frame in the lighting upload; the eye
     *  adaptation step adds nearby point lights to it. */
    float _baseSceneLuminance;
    /** The eye's currently adapted luminance (drives auto-exposure); eased toward the scene luminance over time. */
    float _adaptedLuminance;
    /** false until the first adaptation update snaps _adaptedLuminance to the scene (avoids a first-frame flash). */
    bool _adaptationInitialized;
    /** The atmospheric sky shader drawn behind the geometry; borrowed. NULL = fall back to the gradient clear. */
    GameShader* _skyShader;
    /** The depth-only shader used for the sun shadow-map pass; borrowed. NULL = no shadows. */
    GameShader* _depthShader;
    /** The post-process shader (screen-space AO + hand-drawn outlines); borrowed. NULL = post pass skipped. */
    GameShader* _postfxShader;
    /** The normal/mask G-buffer shader (writes view-space normals + a surface/outline flag for the postfx
     *  outline pass); borrowed. NULL = post pass skipped (the outline pass needs the normal/flag buffer). */
    GameShader* _normalShader;
    /** Cached world_normal outline-flag uniform location (-1 when absent). */
    int _normalLocOutlineFlag;
    /** Set once the shaders' load has been attempted (success or failure), so it is tried only once. */
    bool _shadersLoadAttempted;
    /** Whether hand-drawn outlines are drawn (code-configurable per spec; on by default). */
    bool _outlineEnabled;
    /** Master toggle for the whole post pass (AO + outlines); when false the scene blits straight to the
     *  tonemap, bit-exact with the pre-postfx pipeline. For debugging/A-B comparison; on by default. */
    bool _postfxEnabled;
    /** Cached world_postfx uniform locations (-1 when absent). */
    int _postfxLocResolution;
    int _postfxLocInvProjection;
    int _postfxLocProjection;
    int _postfxLocDepthTexture;
    int _postfxLocNormalTexture;
    int _postfxLocSunDirectionView;
    int _postfxLocAoStrength;
    int _postfxLocOutlineStrength;
    /** Cached world_sky uniform locations (-1 when absent). */
    int _skyLocResolution;
    int _skyLocInvViewProj;
    int _skyLocCameraPos;
    int _skyLocSunDirection;
    int _skyLocSunColor;
    int _skyLocSunIntensity;
    int _skyLocSunSize;
    int _skyLocTurbidity;
    int _skyLocSkyTint;
    int _skyLocStarSeed;
    int _skyLocStarDensity;
    int _skyLocStarBrightness;
    /** Whether the scene target is a floating-point (HDR) framebuffer; false = 8-bit fallback. */
    bool _sceneIsHDR;
    /** Set once the HDR/fallback status has been logged, so it is reported only once. */
    bool _sceneHdrReported;
    /** Cached PBR uniform locations set every frame (-1 when the uniform is absent/optimized out). */
    int _locViewPos;
    int _locSunDirection;
    int _locSunColor;
    int _locSunIntensity;
    int _locAmbientColor;
    int _locAmbientIntensity;
    /** Cached PBR shadow uniform locations (-1 when absent). */
    int _locLightVP;
    int _locShadowMap;
    int _locShadowStrength;
    int _locShadowBias;
    int _locShadowTexelSize;
    /** Cached PBR point-light uniform locations (-1 when absent); uploaded per drawn object after reach-culling. */
    int _locPointLightCount;
    int _locPointLightPositions;
    int _locPointLightRadiances;
    int _locPointLightRanges;
    /** Cached PBR distance-fog uniform locations (-1 when absent). The fog fades distant geometry into the
     *  atmospheric sky colour, so it needs the same sky parameters the sky pass uses (turbidity, tint, raw
     *  sun intensity) plus the fog density. */
    int _locFogDensity;
    int _locFogTurbidity;
    int _locFogSkyTint;
    int _locFogSunIntensity;
    /** Per-asset local bounding-sphere cache (keyed by GameModel*), for point-light reach culling. */
    const void* _boundsCacheAsset[BOUNDS_CACHE_CAPACITY];
    Vector3 _boundsCacheCenter[BOUNDS_CACHE_CAPACITY];
    float _boundsCacheRadius[BOUNDS_CACHE_CAPACITY];
    size_t _boundsCacheCount;
    /** The sun shadow map (depth-only framebuffer); its samplable depth texture is in _shadowMap.depth. */
    RenderTexture2D _shadowMap;
    /** Whether _shadowMap has been created. */
    bool _hasShadowMap;
    /** World -> sun light-clip matrix for the most recent shadow pass (uploaded to the PBR shader). */
    Matrix _lightVP;
    /** Whether shadows are active this frame (sun up + enabled + shadow map ready). */
    bool _shadowActive;
    /** Internal scene target the 3D world renders into (pixel resolution when pixelating). */
    RenderTexture2D _sceneTarget;
    /** Whether _sceneTarget has been created. */
    bool _hasSceneTarget;
    /** Whether _sceneTarget's depth attachment is a samplable texture (required by the postfx pass). */
    bool _sceneDepthSamplable;
    /** Ping target the postfx pass writes into (same size/format as _sceneTarget); only made when postfx runs. */
    RenderTexture2D _postTarget;
    /** Whether _postTarget has been created. */
    bool _hasPostTarget;
    /** Low-res normal/mask G-buffer (RGB=view normal, A=surface/outline flag) the postfx pass reads; only made
     *  when postfx runs. */
    RenderTexture2D _normalTarget;
    /** Whether _normalTarget has been created. */
    bool _hasNormalTarget;
    /** Ping-pong bloom buffers (colour-only HDR, reduced resolution, BILINEAR): prefilter writes A, then the
     *  separable blur ping-pongs A<->B, leaving the final blurred bloom in A. Only made when bloom can run. */
    RenderTexture2D _bloomTargetA;
    RenderTexture2D _bloomTargetB;
    /** Whether the bloom targets have been created. */
    bool _hasBloomTargets;
    /** Size of the bloom buffers, in pixels. */
    int _bloomWidth;
    int _bloomHeight;
    /** Sun-shaft buffer (colour-only HDR, reduced resolution, BILINEAR); only made when sun shafts can run. */
    RenderTexture2D _sunshaftTarget;
    /** Whether the sun-shaft target has been created. */
    bool _hasSunshaftTarget;
    /** Size of the sun-shaft buffer, in pixels. */
    int _sunshaftWidth;
    int _sunshaftHeight;
    /** Crisp overlay target (HDR colour + samplable depth) at WINDOW resolution: the un-pixelated OmitPixelation
     *  objects render here, then composite over the pixelated frame. Only made when the crisp shader is present. */
    RenderTexture2D _crispTarget;
    /** Whether the crisp overlay target has been created, and whether its depth is a samplable texture. */
    bool _hasCrispTarget;
    bool _crispDepthSamplable;
    /** Size of the crisp overlay target, in pixels (matches the window / frame target). */
    int _crispWidth;
    int _crispHeight;
    /** Current width of _sceneTarget, in pixels. */
    int _sceneWidth;
    /** Current height of _sceneTarget, in pixels. */
    int _sceneHeight;
};


// Static functions.
/* Computes the scene-target size for a given window size. With pixelation, the longer window axis is
 * pinned to PIXELATION_LONG_AXIS and the shorter axis derived proportionally, so the upscale to the window
 * is a near-uniform (square-pixel) magnification. Without pixelation the scene target matches the window. */
static void ComputeSceneSize(bool pixelation, int windowWidth, int windowHeight, int* outWidth, int* outHeight)
{
    int SafeWidth = (windowWidth < 1) ? 1 : windowWidth;
    int SafeHeight = (windowHeight < 1) ? 1 : windowHeight;

    if (!pixelation)
    {
        *outWidth = SafeWidth;
        *outHeight = SafeHeight;
        return;
    }

    if (SafeWidth >= SafeHeight)
    {
        *outWidth = PIXELATION_LONG_AXIS;
        *outHeight = (int)lroundf((float)PIXELATION_LONG_AXIS * (float)SafeHeight / (float)SafeWidth);
    }
    else
    {
        *outHeight = PIXELATION_LONG_AXIS;
        *outWidth = (int)lroundf((float)PIXELATION_LONG_AXIS * (float)SafeWidth / (float)SafeHeight);
    }

    if (*outWidth < 1) { *outWidth = 1; }
    if (*outHeight < 1) { *outHeight = 1; }
}

/* Tries to create an HDR (RGBA16F) scene framebuffer. The depth attachment is a samplable depth TEXTURE
 * when @p samplableDepth is true (the postfx pass reads scene depth; depth TESTING is identical either way)
 * or a plain depth renderbuffer otherwise. Returns a zero target (id 0) if the GPU cannot make the
 * combination complete. */
static RenderTexture2D TryCreateHdrSceneTarget(int width, int height, bool samplableDepth)
{
    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return Target;
    }

    rlEnableFramebuffer(Target.id);

    Target.texture.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    Target.texture.width = width;
    Target.texture.height = height;
    Target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    Target.texture.mipmaps = 1;

    Target.depth.id = rlLoadTextureDepth(width, height, !samplableDepth);
    Target.depth.width = width;
    Target.depth.height = height;
    Target.depth.format = SCENE_DEPTH_FORMAT;
    Target.depth.mipmaps = 1;

    rlFramebufferAttach(Target.id, Target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(Target.id, Target.depth.id, RL_ATTACHMENT_DEPTH,
        samplableDepth ? RL_ATTACHMENT_TEXTURE2D : RL_ATTACHMENT_RENDERBUFFER, 0);

    bool Complete = (Target.texture.id != 0) && rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();

    if (!Complete)
    {
        UnloadRenderTexture(Target); // frees the framebuffer and its attachments
        Target = (RenderTexture2D){ 0 };
    }
    return Target;
}

/* Creates the scene render target, degrading gracefully so the post pass can never cost the HDR pipeline:
 * (1) HDR + samplable depth texture (full pipeline incl. postfx), (2) HDR + depth renderbuffer (postfx
 * disabled, sky/tonemap intact — the pre-postfx arrangement), (3) 8-bit LoadRenderTexture as the last
 * resort. An 8-bit scene buffer clamps/quantizes the linear-HDR sky badly, so it must never be entered just
 * because the depth-texture combination failed. */
static RenderTexture2D CreateSceneTarget(int width, int height, bool* outIsHDR, bool* outDepthSamplable)
{
    *outIsHDR = false;
    *outDepthSamplable = false;

    RenderTexture2D Target = TryCreateHdrSceneTarget(width, height, true);
    if (Target.id != 0)
    {
        *outIsHDR = true;
        *outDepthSamplable = true;
        return Target;
    }

    Target = TryCreateHdrSceneTarget(width, height, false);
    if (Target.id != 0)
    {
        *outIsHDR = true;
        return Target;
    }

    return LoadRenderTexture(width, height);
}

/* Creates a colour-only HDR (RGBA16F) framebuffer for the postfx ping target (no depth: the post pass writes
 * every pixel with no depth test). Returns a zero target (id 0) if it could not be made complete. */
static RenderTexture2D CreatePostTarget(int width, int height)
{
    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return Target;
    }

    rlEnableFramebuffer(Target.id);
    Target.texture.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    Target.texture.width = width;
    Target.texture.height = height;
    Target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    Target.texture.mipmaps = 1;
    rlFramebufferAttach(Target.id, Target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    bool Complete = (Target.texture.id != 0) && rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();
    if (!Complete)
    {
        UnloadRenderTexture(Target);
        Target = (RenderTexture2D){ 0 };
    }
    return Target;
}

/* Creates a depth-only framebuffer whose depth attachment is a samplable texture, for the sun shadow map.
 * Returns a zero target (id 0) if the framebuffer could not be created. The depth texture lives in .depth. */
static RenderTexture2D LoadShadowMap(int width, int height)
{
    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return Target;
    }

    rlEnableFramebuffer(Target.id);
    // No colour attachment (depth-only). BeginTextureMode reads .texture size for the viewport, so set it.
    Target.texture.width = width;
    Target.texture.height = height;
    Target.depth.id = rlLoadTextureDepth(width, height, false); // false => a samplable depth TEXTURE
    Target.depth.width = width;
    Target.depth.height = height;
    Target.depth.format = SCENE_DEPTH_FORMAT;
    Target.depth.mipmaps = 1;
    rlFramebufferAttach(Target.id, Target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();
    return Target;
}

/* Ensures the scene target exists and matches the desired size for the given window size / pixelation. */
static void EnsureSceneTarget(WorldRenderer* self, int windowWidth, int windowHeight)
{
    int DesiredWidth = 0;
    int DesiredHeight = 0;
    ComputeSceneSize(self->_pixelationEnabled, windowWidth, windowHeight, &DesiredWidth, &DesiredHeight);

    // The crisp overlay is WINDOW-resolution, so it must also track the raw window size (which can change while
    // the pixelated scene size stays put — same aspect, different window). Guard on both.
    int SafeWindowWidth = (windowWidth < 1) ? 1 : windowWidth;
    int SafeWindowHeight = (windowHeight < 1) ? 1 : windowHeight;
    bool CrispSizeOk = (self->_crispCompositeShader == NULL)
        || (self->_hasCrispTarget && (self->_crispWidth == SafeWindowWidth) && (self->_crispHeight == SafeWindowHeight));

    if (self->_hasSceneTarget && (self->_sceneWidth == DesiredWidth) && (self->_sceneHeight == DesiredHeight)
        && CrispSizeOk)
    {
        return;
    }

    if (self->_hasSceneTarget)
    {
        UnloadRenderTexture(self->_sceneTarget);
        self->_hasSceneTarget = false;
    }
    if (self->_hasPostTarget)
    {
        UnloadRenderTexture(self->_postTarget);
        self->_hasPostTarget = false;
    }
    if (self->_hasNormalTarget)
    {
        UnloadRenderTexture(self->_normalTarget);
        self->_hasNormalTarget = false;
    }
    if (self->_hasBloomTargets)
    {
        UnloadRenderTexture(self->_bloomTargetA);
        UnloadRenderTexture(self->_bloomTargetB);
        self->_hasBloomTargets = false;
    }
    if (self->_hasSunshaftTarget)
    {
        UnloadRenderTexture(self->_sunshaftTarget);
        self->_hasSunshaftTarget = false;
    }
    if (self->_hasCrispTarget)
    {
        UnloadRenderTexture(self->_crispTarget);
        self->_hasCrispTarget = false;
    }

    self->_sceneTarget = CreateSceneTarget(DesiredWidth, DesiredHeight, &self->_sceneIsHDR, &self->_sceneDepthSamplable);
    // Point filtering makes the upscale to the window produce hard, square pixels.
    SetTextureFilter(self->_sceneTarget.texture, TEXTURE_FILTER_POINT);
    if (self->_sceneDepthSamplable)
    {
        // The postfx pass compares raw depth texels, so nearest (point) sampling is required.
        SetTextureFilter(self->_sceneTarget.depth, TEXTURE_FILTER_POINT);
    }
    self->_hasSceneTarget = true;
    self->_sceneWidth = DesiredWidth;
    self->_sceneHeight = DesiredHeight;

    // The postfx ping + normal G-buffer targets: only needed when the post pass can run (postfx + normal
    // shaders present, samplable depth). The normal target is a plain 8-bit RGBA colour+depth RT (it holds an
    // encoded normal + flags, not HDR); its own depth lets the nearest model's normal win per pixel.
    if ((self->_postfxShader != NULL) && (self->_normalShader != NULL) && self->_sceneDepthSamplable)
    {
        self->_postTarget = CreatePostTarget(DesiredWidth, DesiredHeight);
        self->_hasPostTarget = (self->_postTarget.id != 0);
        if (self->_hasPostTarget)
        {
            SetTextureFilter(self->_postTarget.texture, TEXTURE_FILTER_POINT);
        }

        self->_normalTarget = LoadRenderTexture(DesiredWidth, DesiredHeight);
        self->_hasNormalTarget = (self->_normalTarget.id != 0);
        if (self->_hasNormalTarget)
        {
            SetTextureFilter(self->_normalTarget.texture, TEXTURE_FILTER_POINT);
        }
    }

    // Bloom ping-pong buffers (colour-only HDR, reduced resolution). Only when both bloom shaders are present.
    // BILINEAR filtering is required — the blur's fractional-texel taps and the tonemap's bloom upsample both
    // rely on it (the smooth glow is intentionally NOT the pixelated look).
    if ((self->_bloomPrefilterShader != NULL) && (self->_bloomBlurShader != NULL))
    {
        int BloomWidth = DesiredWidth / BLOOM_RESOLUTION_DIVISOR;
        int BloomHeight = DesiredHeight / BLOOM_RESOLUTION_DIVISOR;
        if (BloomWidth < 1) { BloomWidth = 1; }
        if (BloomHeight < 1) { BloomHeight = 1; }
        self->_bloomTargetA = CreatePostTarget(BloomWidth, BloomHeight);
        self->_bloomTargetB = CreatePostTarget(BloomWidth, BloomHeight);
        self->_hasBloomTargets = (self->_bloomTargetA.id != 0) && (self->_bloomTargetB.id != 0);
        if (self->_hasBloomTargets)
        {
            self->_bloomWidth = BloomWidth;
            self->_bloomHeight = BloomHeight;
            SetTextureFilter(self->_bloomTargetA.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(self->_bloomTargetB.texture, TEXTURE_FILTER_BILINEAR);
        }
        else
        {
            // Partial creation: release whichever succeeded so nothing leaks and the pass cleanly skips.
            if (self->_bloomTargetA.id != 0) { UnloadRenderTexture(self->_bloomTargetA); }
            if (self->_bloomTargetB.id != 0) { UnloadRenderTexture(self->_bloomTargetB); }
        }
    }

    // Sun-shaft buffer (colour-only HDR, reduced resolution, BILINEAR). Only when the shaft shader is present.
    if (self->_sunshaftShader != NULL)
    {
        int ShaftWidth = DesiredWidth / SUNSHAFT_RESOLUTION_DIVISOR;
        int ShaftHeight = DesiredHeight / SUNSHAFT_RESOLUTION_DIVISOR;
        if (ShaftWidth < 1) { ShaftWidth = 1; }
        if (ShaftHeight < 1) { ShaftHeight = 1; }
        self->_sunshaftTarget = CreatePostTarget(ShaftWidth, ShaftHeight);
        self->_hasSunshaftTarget = (self->_sunshaftTarget.id != 0);
        if (self->_hasSunshaftTarget)
        {
            self->_sunshaftWidth = ShaftWidth;
            self->_sunshaftHeight = ShaftHeight;
            SetTextureFilter(self->_sunshaftTarget.texture, TEXTURE_FILTER_BILINEAR);
        }
    }

    // Crisp overlay target at WINDOW resolution (not the low pixelation resolution — that is the whole point):
    // an HDR colour + SAMPLABLE depth texture (the composite depth-tests the crisp objects against the scene).
    // BILINEAR so the crisp surface (e.g. a readable screen) is smoothly sampled, not pixelated. Only when the
    // crisp shader is present; if the samplable-depth FBO cannot be made, the crisp overlay stays off.
    if (self->_crispCompositeShader != NULL)
    {
        int CrispWidth = (windowWidth < 1) ? 1 : windowWidth;
        int CrispHeight = (windowHeight < 1) ? 1 : windowHeight;
        self->_crispTarget = TryCreateHdrSceneTarget(CrispWidth, CrispHeight, true);
        self->_hasCrispTarget = (self->_crispTarget.id != 0);
        self->_crispDepthSamplable = self->_hasCrispTarget;
        if (self->_hasCrispTarget)
        {
            self->_crispWidth = CrispWidth;
            self->_crispHeight = CrispHeight;
            SetTextureFilter(self->_crispTarget.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(self->_crispTarget.depth, TEXTURE_FILTER_POINT);
        }
    }

    // Report the scene-target tier once so it is easy to confirm which pipeline is active from the log.
    if (!self->_sceneHdrReported && (self->_logger != NULL))
    {
        self->_sceneHdrReported = true;
        const unsigned char* Message;
        if (self->_sceneIsHDR && self->_sceneDepthSamplable)
        {
            Message = (const unsigned char*)u8"WorldRenderer: scene target is HDR (RGBA16F) with samplable depth (post effects available).";
        }
        else if (self->_sceneIsHDR)
        {
            Message = (const unsigned char*)u8"WorldRenderer: scene target is HDR (RGBA16F); depth not samplable, post effects (AO/outlines) disabled.";
        }
        else
        {
            Message = (const unsigned char*)u8"WorldRenderer: HDR framebuffer unsupported; using 8-bit scene target (post effects disabled).";
        }
        Error LogResult = Logger_LogInfo(self->_logger, Message);
        Error_Deconstruct(&LogResult);
    }
}

/* Logs an asset-resolution failure (if a logger is present) and releases the error. */
static void ReportAssetFailure(WorldRenderer* self, const unsigned char* assetName, Error* error)
{
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"WorldRenderer: could not load model asset \"%s\": %s",
            (const char*)assetName,
            (error->Message != NULL) ? (const char*)error->Message : "no details");
        Error_Deconstruct(&LogResult);
    }
    Error_Deconstruct(error);
}

/* Config-side shadow strength multiplier (1.0 when no config is bound). Combined with the world's own. */
static float ConfigShadowStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->ShadowStrength : 1.0f;
}

/* Config-side ambient-occlusion strength multiplier (1.0 when no config is bound). */
static float ConfigAoStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->AmbientOcclusionStrength : 1.0f;
}

/* Config-side fog strength multiplier (1.0 when no config is bound). */
static float ConfigFogStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->FogStrength : 1.0f;
}

/* Config-side bloom strength multiplier (1.0 when no config is bound). Combined with the world's own. */
static float ConfigBloomStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->BloomStrength : 1.0f;
}

/* Config-side sun-shaft strength multiplier (1.0 when no config is bound). Combined with the world's own. */
static float ConfigSunshaftStrength(const WorldRenderer* self)
{
    return (self->_config != NULL) ? self->_config->SunshaftStrength : 1.0f;
}

/* Converts an 8-bit sRGB colour to linear light in [0,1]^3 (alpha ignored). Light/ambient colours must be
 * linear before they enter the PBR maths; the shader linearizes textured albedo itself. */
static void ColorToLinear(Color color, float outRGB[3])
{
    outRGB[0] = powf((float)color.r / 255.0f, SRGB_GAMMA);
    outRGB[1] = powf((float)color.g / 255.0f, SRGB_GAMMA);
    outRGB[2] = powf((float)color.b / 255.0f, SRGB_GAMMA);
}

/* Converts an opaque sRGB colour to the byte encoding of its LINEAR value, so clearing the linear-HDR scene
 * buffer to a CPU-authored (sRGB) sky colour leaves the buffer consistently linear for the tonemap pass. */
static Color LinearizeColorBytes(Color color)
{
    return (Color)
    {
        .r = (unsigned char)lroundf(powf((float)color.r / 255.0f, SRGB_GAMMA) * 255.0f),
        .g = (unsigned char)lroundf(powf((float)color.g / 255.0f, SRGB_GAMMA) * 255.0f),
        .b = (unsigned char)lroundf(powf((float)color.b / 255.0f, SRGB_GAMMA) * 255.0f),
        .a = color.a
    };
}

/* Loads a shader asset by name under the renderer's user, logging and swallowing failure (returns NULL). */
static GameShader* LoadRendererShader(WorldRenderer* self, const unsigned char* assetName)
{
    GameShader* LoadedShader = NULL;
    Error LoadResult = AssetManager_LoadShader(self->_assetManager, assetName, self->_assetUser, &LoadedShader);
    if (LoadResult.Code != ErrorCode_Success)
    {
        if (self->_logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(self->_logger,
                (const unsigned char*)u8"WorldRenderer: shader \"%s\" unavailable (%s).",
                (const char*)assetName,
                (LoadResult.Message != NULL) ? (const char*)LoadResult.Message : "no details");
            Error_Deconstruct(&LogResult);
        }
        Error_Deconstruct(&LoadResult);
        return NULL;
    }
    return LoadedShader;
}

/* Loads the PBR + tonemap shaders once (idempotent). If PBR is unavailable models draw with Raylib's
 * default (unlit) shader; if the tonemap is unavailable the HDR scene is blitted untonemapped. Caches the
 * PBR per-frame uniform locations and uploads its constant material parameters. Needs a live GL context. */
static void EnsureShaders(WorldRenderer* self)
{
    if (self->_shadersLoadAttempted)
    {
        return;
    }
    self->_shadersLoadAttempted = true;

    self->_pbrShader = LoadRendererShader(self, PBR_SHADER_ASSET_NAME);
    if (self->_pbrShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_pbrShader);

        self->_locViewPos = GetShaderLocation(RayShader, "viewPos");
        self->_locSunDirection = GetShaderLocation(RayShader, "sunDirection");
        self->_locSunColor = GetShaderLocation(RayShader, "sunColor");
        self->_locSunIntensity = GetShaderLocation(RayShader, "sunIntensity");
        self->_locAmbientColor = GetShaderLocation(RayShader, "ambientColor");
        self->_locAmbientIntensity = GetShaderLocation(RayShader, "ambientIntensity");

        self->_locLightVP = GetShaderLocation(RayShader, "lightVP");
        self->_locShadowMap = GetShaderLocation(RayShader, "shadowMap");
        self->_locShadowStrength = GetShaderLocation(RayShader, "shadowStrength");
        self->_locShadowBias = GetShaderLocation(RayShader, "shadowBias");
        self->_locShadowTexelSize = GetShaderLocation(RayShader, "shadowTexelSize");

        self->_locPointLightCount = GetShaderLocation(RayShader, "pointLightCount");
        self->_locPointLightPositions = GetShaderLocation(RayShader, "pointLightPositions");
        self->_locPointLightRadiances = GetShaderLocation(RayShader, "pointLightRadiances");
        self->_locPointLightRanges = GetShaderLocation(RayShader, "pointLightRanges");

        self->_locFogDensity = GetShaderLocation(RayShader, "fogDensity");
        self->_locFogTurbidity = GetShaderLocation(RayShader, "skyTurbidity");
        self->_locFogSkyTint = GetShaderLocation(RayShader, "skyTint");
        self->_locFogSunIntensity = GetShaderLocation(RayShader, "skySunIntensity");

        // Constant material + shadow parameters; upload once.
        float Metallic = PBR_DEFAULT_METALLIC;
        float Roughness = PBR_DEFAULT_ROUGHNESS;
        float Ao = PBR_DEFAULT_AO;
        float EmissiveColor[3] = { 0.0f, 0.0f, 0.0f };
        float EmissiveIntensity = 0.0f;
        float ShadowBias = SHADOW_BIAS;
        float ShadowTexel = 1.0f / (float)SHADOW_MAP_SIZE;
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "metallic"), &Metallic, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "roughness"), &Roughness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "ao"), &Ao, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "emissiveColor"), EmissiveColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "emissiveIntensity"), &EmissiveIntensity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, self->_locShadowBias, &ShadowBias, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, self->_locShadowTexelSize, &ShadowTexel, SHADER_UNIFORM_FLOAT);
    }

    self->_tonemapShader = LoadRendererShader(self, TONEMAP_SHADER_ASSET_NAME);
    if (self->_tonemapShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_tonemapShader);
        self->_tonemapLocExposure = GetShaderLocation(RayShader, "exposure");
        self->_tonemapLocBloomTexture = GetShaderLocation(RayShader, "bloomTexture");
        self->_tonemapLocBloomStrength = GetShaderLocation(RayShader, "bloomStrength");
        self->_tonemapLocSunshaftTexture = GetShaderLocation(RayShader, "sunshaftTexture");
        self->_tonemapLocSunshaftStrength = GetShaderLocation(RayShader, "sunshaftStrength");
    }

    // Bloom shaders (bright-pass prefilter + separable blur). Optional: without them (or without the tonemap's
    // bloom uniforms, or the reduced-res bloom targets) the bloom pass is skipped and the scene tonemaps as-is.
    self->_bloomPrefilterShader = LoadRendererShader(self, BLOOM_PREFILTER_SHADER_ASSET_NAME);
    if (self->_bloomPrefilterShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_bloomPrefilterShader);
        self->_bloomPrefilterLocResolution = GetShaderLocation(RayShader, "resolution");
        self->_bloomPrefilterLocThreshold = GetShaderLocation(RayShader, "threshold");
        self->_bloomPrefilterLocSoftKnee = GetShaderLocation(RayShader, "softKnee");
        self->_bloomPrefilterLocDepthTexture = GetShaderLocation(RayShader, "depthTexture");
        self->_bloomPrefilterLocDepthMask = GetShaderLocation(RayShader, "depthMask");
    }
    self->_bloomBlurShader = LoadRendererShader(self, BLOOM_BLUR_SHADER_ASSET_NAME);
    if (self->_bloomBlurShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_bloomBlurShader);
        self->_bloomBlurLocResolution = GetShaderLocation(RayShader, "resolution");
        self->_bloomBlurLocDirection = GetShaderLocation(RayShader, "direction");
    }

    // Sun-shafts (god rays) shader. Optional: without it (or the tonemap's sunshaft uniforms, or the shaft
    // target, or samplable depth) the pass is skipped and the scene tonemaps without shafts.
    self->_sunshaftShader = LoadRendererShader(self, SUNSHAFT_SHADER_ASSET_NAME);
    if (self->_sunshaftShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_sunshaftShader);
        self->_sunshaftLocResolution = GetShaderLocation(RayShader, "resolution");
        self->_sunshaftLocDepthTexture = GetShaderLocation(RayShader, "depthTexture");
        self->_sunshaftLocSunScreenPos = GetShaderLocation(RayShader, "sunScreenPos");
    }

    // Crisp-composite shader (un-pixelated OmitPixelation objects over the pixelated frame). Optional: without
    // it (or a samplable-depth crisp target) OmitPixelation objects just render pixelated with the rest.
    self->_crispCompositeShader = LoadRendererShader(self, CRISP_COMPOSITE_SHADER_ASSET_NAME);
    if (self->_crispCompositeShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_crispCompositeShader);
        self->_crispLocCrispDepth = GetShaderLocation(RayShader, "crispDepth");
        self->_crispLocSceneDepth = GetShaderLocation(RayShader, "sceneDepth");
        self->_crispLocExposure = GetShaderLocation(RayShader, "exposure");
    }

    // Sprite shader (linearizes sprite sRGB art into the HDR scene). Optional: without it sprites fall back to
    // Raylib's default (unlit) shader; it needs no cached uniform locations (texture0/colDiffuse are built-in).
    self->_spriteShader = LoadRendererShader(self, SPRITE_SHADER_ASSET_NAME);

    // Depth shader + shadow map for the sun shadow pass (optional; models draw unshadowed without them).
    self->_depthShader = LoadRendererShader(self, DEPTH_SHADER_ASSET_NAME);
    if ((self->_depthShader != NULL) && !self->_hasShadowMap)
    {
        self->_shadowMap = LoadShadowMap(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        self->_hasShadowMap = (self->_shadowMap.id != 0);
        if (self->_hasShadowMap)
        {
            // Manual PCF compares raw depth texels, so nearest (point) sampling is required; linear
            // filtering would interpolate depths and corrupt the comparison.
            SetTextureFilter(self->_shadowMap.depth, TEXTURE_FILTER_POINT);
        }
    }

    self->_skyShader = LoadRendererShader(self, SKY_SHADER_ASSET_NAME);
    if (self->_skyShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_skyShader);
        self->_skyLocResolution = GetShaderLocation(RayShader, "resolution");
        self->_skyLocInvViewProj = GetShaderLocation(RayShader, "invViewProj");
        self->_skyLocCameraPos = GetShaderLocation(RayShader, "cameraPos");
        self->_skyLocSunDirection = GetShaderLocation(RayShader, "sunDirection");
        self->_skyLocSunColor = GetShaderLocation(RayShader, "sunColor");
        self->_skyLocSunIntensity = GetShaderLocation(RayShader, "sunIntensity");
        self->_skyLocSunSize = GetShaderLocation(RayShader, "sunSize");
        self->_skyLocTurbidity = GetShaderLocation(RayShader, "turbidity");
        self->_skyLocSkyTint = GetShaderLocation(RayShader, "skyTint");
        self->_skyLocStarSeed = GetShaderLocation(RayShader, "starSeed");
        self->_skyLocStarDensity = GetShaderLocation(RayShader, "starDensity");
        self->_skyLocStarBrightness = GetShaderLocation(RayShader, "starBrightness");
    }

    // Post-process shader (screen-space AO + hand-drawn outlines). Optional: without it the scene is blitted
    // straight to the tonemap pass. It also needs the samplable-depth HDR scene target (checked at use time).
    self->_postfxShader = LoadRendererShader(self, POSTFX_SHADER_ASSET_NAME);
    if (self->_postfxShader != NULL)
    {
        Shader RayShader = GameShader_GetRaylibShader(self->_postfxShader);
        self->_postfxLocResolution = GetShaderLocation(RayShader, "resolution");
        self->_postfxLocInvProjection = GetShaderLocation(RayShader, "invProjection");
        self->_postfxLocProjection = GetShaderLocation(RayShader, "projection");
        self->_postfxLocDepthTexture = GetShaderLocation(RayShader, "depthTexture");
        self->_postfxLocNormalTexture = GetShaderLocation(RayShader, "normalTexture");
        self->_postfxLocSunDirectionView = GetShaderLocation(RayShader, "sunDirectionView");
        self->_postfxLocAoStrength = GetShaderLocation(RayShader, "aoStrength");
        self->_postfxLocOutlineStrength = GetShaderLocation(RayShader, "outlineStrength");
    }

    // Normal/mask G-buffer shader: draws each world model's view-space normal + a surface/outline flag so the
    // postfx pass can edge-detect outlines and restrict AO to objects (not the sky/grid/shadows). Without it
    // the post pass is skipped.
    self->_normalShader = LoadRendererShader(self, NORMAL_SHADER_ASSET_NAME);
    if (self->_normalShader != NULL)
    {
        self->_normalLocOutlineFlag = GetShaderLocation(GameShader_GetRaylibShader(self->_normalShader), "outlineFlag");
    }
}

/* Uploads the per-frame PBR lighting uniforms (camera position, sun and ambient light) from the world's
 * environment. No-op when the PBR shader is unavailable. */
static void UpdateLightingUniforms(WorldRenderer* self, World* world, const GameCamera* camera)
{
    if (self->_pbrShader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(self->_pbrShader);
    const WorldEnvironment* Environment = World_GetEnvironment(world);

    float ViewPos[3] = { camera->Position.x, camera->Position.y, camera->Position.z };
    SetShaderValue(RayShader, self->_locViewPos, ViewPos, SHADER_UNIFORM_VEC3);

    Vector3 SunDirection = WorldEnvironment_GetSunDirection(Environment);
    float SunDir[3] = { SunDirection.x, SunDirection.y, SunDirection.z };
    SetShaderValue(RayShader, self->_locSunDirection, SunDir, SHADER_UNIFORM_VEC3);

    // Day-night cycle: the sun stops lighting objects once it is below the horizon, and the ambient skylight
    // fades from its authored daytime colour to a dim night ambient. Without this the constant ambient kept
    // objects fully bright and daytime-blue-tinted all night.
    float Daylight = WorldEnvironment_GetDaylightFactor(Environment); // 1 day .. 0 night

    float SunColor[3];
    ColorToLinear(Environment->SunColor, SunColor);
    SetShaderValue(RayShader, self->_locSunColor, SunColor, SHADER_UNIFORM_VEC3);
    float EffectiveSunIntensity = Environment->SunIntensity * Daylight;
    SetShaderValue(RayShader, self->_locSunIntensity, &EffectiveSunIntensity, SHADER_UNIFORM_FLOAT);

    // Ambient blended day->night, folded into the colour (intensity kept at 1). Daytime = authored skylight ×
    // its intensity; night = the dim cool NIGHT_AMBIENT.
    // Ambient skylight is built to TRACK the sky's timing, driven by the sun's elevation and the SAME daylight
    // thresholds the sun lighting uses (WorldEnvironment_GetDaylightFactor), so the fill light and the sun agree:
    //   * day  (sun well above the horizon) -> the daytime blue fill, at full brightness;
    //   * twilight (sun near the horizon)   -> blended toward the warm sunrise/sunset colour;
    //   * night (sun below the horizon)     -> the fill fades out to the dim NIGHT_AMBIENT floor.
    // The warm tint is gated by BOTH a narrow band around the horizon (AMBIENT_SUNSET_BAND, a Gaussian of the
    // sun elevation) AND the daylight factor, so it can neither appear while the sun is still high (sky still
    // blue) nor linger once the sun has set — the two bugs the flat sky-colour gradient produced. The hues are
    // authored (AMBIENT_DAY_* / AMBIENT_SUNSET_*), tinted by AmbientSkylightColor (WHITE = neutral) and scaled
    // by AmbientSkylightIntensity x AMBIENT_SKY_STRENGTH; the whole fill fades with the daylight factor.
    float Elevation = SunDirection.y; // sun elevation in [-1, 1]; 0 = on the horizon
    float SunsetT = Elevation / AMBIENT_SUNSET_BAND;
    float Sunset = expf(-(SunsetT * SunsetT)) * Daylight; // warm only near the horizon AND while it is still day
    if (Sunset < 0.0f) { Sunset = 0.0f; }
    if (Sunset > 1.0f) { Sunset = 1.0f; }

    float DayHue[3] = { AMBIENT_DAY_R, AMBIENT_DAY_G, AMBIENT_DAY_B };
    float SunsetHue[3] = { AMBIENT_SUNSET_R, AMBIENT_SUNSET_G, AMBIENT_SUNSET_B };
    float AmbientTint[3];
    ColorToLinear(Environment->AmbientSkylightColor, AmbientTint);
    float NightAmbient[3] = { NIGHT_AMBIENT_R, NIGHT_AMBIENT_G, NIGHT_AMBIENT_B };
    float AmbientMagnitude = Daylight * Environment->AmbientSkylightIntensity * AMBIENT_SKY_STRENGTH;

    float AmbientColor[3];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        float Hue = DayHue[Channel] + (SunsetHue[Channel] - DayHue[Channel]) * Sunset;
        float Lit = Hue * AmbientTint[Channel] * AmbientMagnitude;
        AmbientColor[Channel] = fmaxf(Lit, NightAmbient[Channel]);
    }
    SetShaderValue(RayShader, self->_locAmbientColor, AmbientColor, SHADER_UNIFORM_VEC3);
    float AmbientIntensity = 1.0f;
    SetShaderValue(RayShader, self->_locAmbientIntensity, &AmbientIntensity, SHADER_UNIFORM_FLOAT);

    // Stash the base scene-illumination luminance (ambient + weighted sun) for the eye-adaptation step, which
    // adds nearby point lights and eases the exposure toward it.
    float SunLum = (0.2126f*SunColor[0] + 0.7152f*SunColor[1] + 0.0722f*SunColor[2]) * EffectiveSunIntensity;
    float AmbientLum = 0.2126f*AmbientColor[0] + 0.7152f*AmbientColor[1] + 0.0722f*AmbientColor[2];
    self->_baseSceneLuminance = AmbientLum + SunLum*SUN_ADAPT_WEIGHT;

    // Atmospheric distance fog: the PBR shader fades distant geometry into the SAME sky the sky pass draws,
    // evaluated per fragment along its view ray, so far objects dissolve seamlessly into the sky (and, because
    // the atmospheric sky is already dark at night, distant geometry fades to the dark night sky on its own).
    // Feed it the sky parameters the sky pass uses (turbidity, linear tint, RAW — not day-scaled — sun
    // intensity); the sun direction is the one uploaded above. Density = base × world × config fog strength.
    SetShaderValue(RayShader, self->_locFogTurbidity, &Environment->SkyTurbidity, SHADER_UNIFORM_FLOAT);
    float FogSkyTint[3];
    ColorToLinear(Environment->SkyTint, FogSkyTint);
    SetShaderValue(RayShader, self->_locFogSkyTint, FogSkyTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_locFogSunIntensity, &Environment->SunIntensity, SHADER_UNIFORM_FLOAT);
    float FogDensity = BASE_FOG_DENSITY * Environment->FogStrength * ConfigFogStrength(self);
    if (FogDensity < 0.0f) { FogDensity = 0.0f; }
    SetShaderValue(RayShader, self->_locFogDensity, &FogDensity, SHADER_UNIFORM_FLOAT);

    // Sun shadow: upload the light matrix and bind the depth map (to a slot clear of the material maps) when
    // shadows are active this frame; otherwise pass strength 0 so the PBR shader skips shadow sampling.
    if (self->_shadowActive)
    {
        SetShaderValueMatrix(RayShader, self->_locLightVP, self->_lightVP);
        rlActiveTextureSlot(SHADOW_TEXTURE_SLOT);
        rlEnableTexture(self->_shadowMap.depth.id);
        int Slot = SHADOW_TEXTURE_SLOT;
        rlSetUniform(self->_locShadowMap, &Slot, SHADER_UNIFORM_INT, 1);
    }
    float ShadowStrength = self->_shadowActive ? (Environment->ShadowStrength * ConfigShadowStrength(self)) : 0.0f;
    SetShaderValue(RayShader, self->_locShadowStrength, &ShadowStrength, SHADER_UNIFORM_FLOAT);
}

/* Eases the adapted luminance toward the scene luminance over @p dt seconds. Slow in general, but faster the
 * larger the brightness change, and faster still when BRIGHTENING (facing a light) than when dark-adapting —
 * so a jump from a black night to a bright light snaps down quickly while dark-adaptation stays gradual. */
static float AdaptLuminance(float adapted, float target, float dt)
{
    if (dt <= 0.0f)
    {
        return adapted;
    }
    if (dt > ADAPT_MAX_DELTA_SECONDS)
    {
        dt = ADAPT_MAX_DELTA_SECONDS;
    }
    float Delta = target - adapted;
    float Speed = ADAPT_BASE_SPEED * (1.0f + ADAPT_DELTA_GAIN * fabsf(Delta));
    if (Delta > 0.0f)
    {
        Speed *= ADAPT_BRIGHTEN_MULTIPLIER;
    }
    float Rate = 1.0f - expf(-dt * Speed);
    if (Rate < 0.0f) { Rate = 0.0f; }
    if (Rate > 1.0f) { Rate = 1.0f; }
    return adapted + Delta * Rate;
}

/* Maps an adapted luminance to a tonemap exposure: reference/adapted, clamped. Day-level luminance gives ~1,
 * a dark night gives a large exposure (revealing the dim ambient), a bright light gives a small one. */
static float ExposureFromAdapted(float adapted)
{
    float Luminance = (adapted < ADAPT_MIN_LUMINANCE) ? ADAPT_MIN_LUMINANCE : adapted;
    float Exposure = ADAPT_REFERENCE_LUMINANCE / Luminance;
    if (Exposure < ADAPT_MIN_EXPOSURE) { Exposure = ADAPT_MIN_EXPOSURE; }
    if (Exposure > ADAPT_MAX_EXPOSURE) { Exposure = ADAPT_MAX_EXPOSURE; }
    return Exposure;
}

/* Updates HDR eye adaptation for the frame and returns the exposure for the tonemap. Starts from the base
 * scene luminance (ambient + sun, set by UpdateLightingUniforms), adds nearby point lights (being near a
 * light raises the estimate → lowers exposure), then eases the adapted luminance toward it. */
static float UpdateEyeAdaptation(WorldRenderer* self, World* world, const GameCamera* camera, float dt)
{
    float SceneLuminance = self->_baseSceneLuminance;

    size_t ObjectCount = World_GetObjectCount(world);
    for (size_t Index = 0; Index < ObjectCount; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }
        if (WorldObject_GetType(Object) != WorldObjectType_Light)
        {
            continue;
        }

        WorldLight* Light = (WorldLight*)Object;
        float Range = WorldLight_GetSize(Light);
        float Intensity = WorldLight_GetIntensity(Light);
        if ((Range <= 0.0f) || (Intensity <= 0.0f))
        {
            continue;
        }
        float Distance = Vector3Distance(WorldObject_GetPosition(Object), camera->Position);
        if (Distance >= Range)
        {
            continue;
        }
        float LinearColor[3];
        ColorToLinear(Light->Color, LinearColor);
        float LightLum = 0.2126f*LinearColor[0] + 0.7152f*LinearColor[1] + 0.0722f*LinearColor[2];
        float Proximity = 1.0f - (Distance / Range);
        SceneLuminance += LightLum*Intensity*Proximity*Proximity*POINT_ADAPT_WEIGHT;
    }

    if (!self->_adaptationInitialized)
    {
        self->_adaptedLuminance = SceneLuminance; // snap on the first frame so there is no start-up flash
        self->_adaptationInitialized = true;
    }
    else
    {
        self->_adaptedLuminance = AdaptLuminance(self->_adaptedLuminance, SceneLuminance, dt);
    }
    return ExposureFromAdapted(self->_adaptedLuminance);
}

/* Returns the model's LOCAL (pre-transform) bounding-sphere centre + radius, caching per asset so the mesh
 * vertices are scanned only once (GetModelBoundingBox re-walks every vertex). Uses an identity transform so
 * the result is the raw mesh-local AABB regardless of the raylib version's transform handling. */
static void GetAssetLocalBounds(WorldRenderer* self, const GameModel* asset, Model rayModel,
    Vector3* outCenter, float* outRadius)
{
    for (size_t Index = 0; Index < self->_boundsCacheCount; Index++)
    {
        if (self->_boundsCacheAsset[Index] == asset)
        {
            *outCenter = self->_boundsCacheCenter[Index];
            *outRadius = self->_boundsCacheRadius[Index];
            return;
        }
    }

    Model LocalModel = rayModel;
    LocalModel.transform = MatrixIdentity();
    BoundingBox Box = GetModelBoundingBox(LocalModel);
    Vector3 Center = { (Box.min.x + Box.max.x)*0.5f, (Box.min.y + Box.max.y)*0.5f, (Box.min.z + Box.max.z)*0.5f };
    Vector3 Half = { (Box.max.x - Box.min.x)*0.5f, (Box.max.y - Box.min.y)*0.5f, (Box.max.z - Box.min.z)*0.5f };
    float Radius = Vector3Length(Half);

    if (self->_boundsCacheCount < BOUNDS_CACHE_CAPACITY)
    {
        size_t Slot = self->_boundsCacheCount++;
        self->_boundsCacheAsset[Slot] = asset;
        self->_boundsCacheCenter[Slot] = Center;
        self->_boundsCacheRadius[Slot] = Radius;
    }
    *outCenter = Center;
    *outRadius = Radius;
}

/* Largest axis scale factor of a transform (longest of its three basis-vector columns), for conservatively
 * scaling a local bounding-sphere radius into world space under non-uniform scale + rotation. */
static float MatrixMaxScale(Matrix m)
{
    float ScaleX = Vector3Length((Vector3){ m.m0, m.m1, m.m2 });
    float ScaleY = Vector3Length((Vector3){ m.m4, m.m5, m.m6 });
    float ScaleZ = Vector3Length((Vector3){ m.m8, m.m9, m.m10 });
    return fmaxf(ScaleX, fmaxf(ScaleY, ScaleZ));
}

/* Reach-culls the world's point lights against an object's world bounding sphere and uploads the strongest
 * few (up to WORLD_MAX_FORWARD_LIGHTS) to the PBR shader for forward shading. Called once per object drawn in
 * the scene pass, immediately before its DrawModel so the per-object light set is in effect for that draw. */
static void UploadPointLightsForObject(WorldRenderer* self, World* world, Vector3 worldCenter, float worldRadius)
{
    const WorldLight* Lights[WORLD_MAX_FORWARD_LIGHTS];
    size_t Count = WorldLightCulling_SelectForSphere(world, worldCenter, worldRadius, Lights, WORLD_MAX_FORWARD_LIGHTS);

    Shader RayShader = GameShader_GetRaylibShader(self->_pbrShader);

    float Positions[WORLD_MAX_FORWARD_LIGHTS * 3];
    float Radiances[WORLD_MAX_FORWARD_LIGHTS * 3];
    float Ranges[WORLD_MAX_FORWARD_LIGHTS];
    for (size_t Index = 0; Index < Count; Index++)
    {
        const WorldLight* Light = Lights[Index];
        Vector3 Position = WorldObject_GetPosition((const WorldObject*)Light); // base is the first member
        Positions[Index*3 + 0] = Position.x;
        Positions[Index*3 + 1] = Position.y;
        Positions[Index*3 + 2] = Position.z;

        float LinearColor[3];
        ColorToLinear(Light->Color, LinearColor);
        float Intensity = WorldLight_GetIntensity(Light);
        Radiances[Index*3 + 0] = LinearColor[0]*Intensity;
        Radiances[Index*3 + 1] = LinearColor[1]*Intensity;
        Radiances[Index*3 + 2] = LinearColor[2]*Intensity;

        Ranges[Index] = WorldLight_GetSize(Light);
    }

    int CountInt = (int)Count;
    SetShaderValue(RayShader, self->_locPointLightCount, &CountInt, SHADER_UNIFORM_INT);
    if (Count > 0)
    {
        SetShaderValueV(RayShader, self->_locPointLightPositions, Positions, SHADER_UNIFORM_VEC3, (int)Count);
        SetShaderValueV(RayShader, self->_locPointLightRadiances, Radiances, SHADER_UNIFORM_VEC3, (int)Count);
        SetShaderValueV(RayShader, self->_locPointLightRanges, Ranges, SHADER_UNIFORM_FLOAT, (int)Count);
    }
}

/* Draws a single model object through @p shader (bound onto each material slot; NULL keeps Raylib's default
 * shader). Used by the scene pass (PBR shader) and the shadow depth pass (depth shader). For the PBR pass it
 * also reach-culls and uploads this object's point lights. Missing/failed assets are skipped silently
 * (PrepareWorld reports them). */
static void DrawModelObjectWithShader(WorldRenderer* self, World* world, WorldModelObject* modelObject, GameShader* shader)
{
    const unsigned char* AssetName = WorldModelObject_GetModelAssetName(modelObject);
    if (AssetName == NULL)
    {
        return;
    }

    GameModel* ModelAsset = NULL;
    Error LoadResult = AssetManager_LoadModel(self->_assetManager, AssetName, self->_assetUser, &ModelAsset);
    if (LoadResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&LoadResult);
        return;
    }

    WorldObject* Base = WorldModelObject_AsObject(modelObject);
    Vector3 Position = WorldObject_GetPosition(Base);
    Vector3 Rotation = WorldObject_GetRotation(Base);
    Vector3 Scale = WorldObject_GetScale(Base);

    Matrix World = MatrixMultiply(MatrixMultiply(MatrixScale(Scale.x, Scale.y, Scale.z),
        MatrixRotateXYZ(Rotation)), MatrixTranslate(Position.x, Position.y, Position.z));

    // Compose over the model's baked import transform (import inner, world outer), matching raylib's own
    // DrawModelEx convention, then draw with a neutral extra transform so only our matrix and tint apply.
    Model RayModel = GameModel_GetRaylibModel(ModelAsset);
    Matrix FinalTransform = MatrixMultiply(RayModel.transform, World);

    // Bind the given shader onto every material slot so DrawModel shades through it. The by-value model copy
    // shares the asset's materials array, so this persists on the asset (fine: the next pass rebinds it).
    // A NULL shader keeps Raylib's default (unlit) so the model still draws.
    if (shader != NULL)
    {
        Shader RayShaderHandle = GameShader_GetRaylibShader(shader);
        for (int MaterialIndex = 0; MaterialIndex < RayModel.materialCount; MaterialIndex++)
        {
            RayModel.materials[MaterialIndex].shader = RayShaderHandle;
        }
    }

    // Scene (PBR) pass only: reach-cull this object's point lights and upload them before the draw. The depth
    // and normal passes do not light, so they skip this.
    if ((shader == self->_pbrShader) && (self->_pbrShader != NULL) && (self->_locPointLightCount >= 0))
    {
        Vector3 LocalCenter;
        float LocalRadius;
        GetAssetLocalBounds(self, ModelAsset, RayModel, &LocalCenter, &LocalRadius);
        Vector3 WorldCenter = Vector3Transform(LocalCenter, FinalTransform);
        float WorldRadius = LocalRadius * MatrixMaxScale(FinalTransform);
        UploadPointLightsForObject(self, world, WorldCenter, WorldRadius);
    }

    RayModel.transform = FinalTransform;
    Color Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Base));
    DrawModel(RayModel, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Tint);
}

/* Whether a model object should be drawn under the given OmitPixelation filter. */
static bool ModelPassesFilter(const WorldModelObject* modelObject, ModelPixelationFilter filter)
{
    switch (filter)
    {
        case ModelPixelationFilter_PixelatedOnly: return !modelObject->OmitPixelation;
        case ModelPixelationFilter_CrispOnly:     return modelObject->OmitPixelation;
        case ModelPixelationFilter_All:
        default:                                  return true;
    }
}

/* Draws the world's model objects through @p shader, restricted to those passing @p filter (by their
 * OmitPixelation flag). Sprites/lights are not drawn here yet. */
static void DrawWorldModels(WorldRenderer* self, World* world, GameShader* shader, ModelPixelationFilter filter)
{
    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }
        if ((WorldObject_GetType(Object) == WorldObjectType_Model)
            && ModelPassesFilter((WorldModelObject*)Object, filter))
        {
            DrawModelObjectWithShader(self, world, (WorldModelObject*)Object, shader);
        }
    }
}

/* Draws the world's sprite objects as camera-facing billboards through the sprite shader (which linearizes
 * their sRGB art into the linear-HDR scene). The (static) first frame of each sprite's animation is drawn for
 * now; per-object animation PLAYBACK is a follow-up (it needs a decision on where per-object playback state
 * lives, since the data layer is deliberately render-independent). The billboard is sized by the object's
 * X/Y scale and tinted by its tint; it depth-tests against the rest of the scene. Must run inside BeginMode3D. */
static void DrawWorldSprites(WorldRenderer* self, World* world, const GameCamera* camera)
{
    Camera3D RayCam = GameCamera_ToRaylibCamera(camera);
    if (self->_spriteShader != NULL)
    {
        BeginShaderMode(GameShader_GetRaylibShader(self->_spriteShader));
    }

    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }
        if (WorldObject_GetType(Object) != WorldObjectType_Sprite)
        {
            continue;
        }

        WorldSpriteObject* SpriteObject = (WorldSpriteObject*)Object;
        const unsigned char* AssetName = WorldSpriteObject_GetSpriteAnimationAssetName(SpriteObject);
        if (AssetName == NULL)
        {
            continue;
        }

        SpriteAnimation* Animation = NULL;
        Error LoadResult = AssetManager_LoadSpriteAnimation(self->_assetManager, AssetName, self->_assetUser, &Animation);
        if (LoadResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&LoadResult);
            continue;
        }
        if (SpriteAnimation_GetFrameCount(Animation) == 0)
        {
            continue;
        }

        SpriteAnimationFrame Frame;
        Error FrameResult = SpriteAnimation_GetFrameAt(Animation, 0, &Frame); // TODO: per-object animation playback
        if (FrameResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&FrameResult);
            continue;
        }

        Vector3 Position = WorldObject_GetPosition(Object);
        Vector3 Scale = WorldObject_GetScale(Object);
        Vector2 Size = { Scale.x, Scale.y };
        Color Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Object));
        DrawBillboardRec(RayCam, Frame._texture, Frame._areaInTexture, Position, Size, Tint);
    }

    if (self->_spriteShader != NULL)
    {
        EndShaderMode();
    }
}

/* Draws a small wireframe sphere at each light's position (tinted by the light colour) as a debug authoring
 * aid — lights are otherwise invisible (they only contribute to shading). Only called with the debug grid on.
 * Must run inside BeginMode3D. */
static void DrawLightGizmos(World* world)
{
    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }
        if (WorldObject_GetType(Object) != WorldObjectType_Light)
        {
            continue;
        }
        WorldLight* Light = (WorldLight*)Object;
        DrawSphereWires(WorldObject_GetPosition(Object), LIGHT_GIZMO_RADIUS, 6, 6, Light->Color);
    }
}

/* Draws every world MODEL object into the normal G-buffer through the normal shader, setting the per-object
 * outline flag uniform (1 when the object has outlines enabled, else 0) before each draw. The normal shader
 * writes RGB=view-space normal, A=surface/outline flag (0.5 = surface, 1.0 = surface with outline); pixels the
 * sky/grid leave untouched stay at the cleared 0 (not a surface). */
static void DrawNormalBufferModels(WorldRenderer* self, World* world, ModelPixelationFilter filter)
{
    Shader RayNormalShader = GameShader_GetRaylibShader(self->_normalShader);
    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }
        if (WorldObject_GetType(Object) != WorldObjectType_Model)
        {
            continue;
        }

        WorldModelObject* ModelObject = (WorldModelObject*)Object;
        if (!ModelPassesFilter(ModelObject, filter))
        {
            continue;
        }
        float OutlineFlag = ModelObject->HasOutline ? 1.0f : 0.0f;
        SetShaderValue(RayNormalShader, self->_normalLocOutlineFlag, &OutlineFlag, SHADER_UNIFORM_FLOAT);
        DrawModelObjectWithShader(self, world, ModelObject, self->_normalShader);
    }
}

/* Renders the normal/mask G-buffer (view-space normals + a surface/outline flag) into _normalTarget with the
 * scene camera, so the postfx pass can edge-detect outlines and restrict AO to world objects. Opens/closes its
 * own passes. Uses its own depth (the nearest model's normal wins; the debug grid is intentionally not drawn
 * here so it never gets outlined). Colour blending is DISABLED so the encoded normal (RGB) and the flag (A,
 * which is 0.5 for non-outline surfaces) are written verbatim — with blending on, an alpha of 0.5 would blend
 * the normal with the cleared background and corrupt it. */
static void RenderNormalBuffer(WorldRenderer* self, World* world, const GameCamera* camera, ModelPixelationFilter filter)
{
    BeginTextureMode(self->_normalTarget);
    ClearBackground(BLANK); // RGBA (0,0,0,0): A=0 = "not a surface" everywhere the geometry does not draw
    rlDisableColorBlend();  // write RGB (normal) + A (flag) directly; no alpha blend
    BeginMode3D(GameCamera_ToRaylibCamera(camera));
    DrawNormalBufferModels(self, world, filter);
    EndMode3D();
    rlEnableColorBlend();   // restore the default blending for the following 2D passes
    EndTextureMode();
}

/* Draws the atmospheric sky as a full-screen pass into the currently bound scene target, reconstructing each
 * pixel's world ray from the inverse view-projection so the sky lines up with the 3D camera. Outputs linear
 * HDR. Must run after the depth clear and BEFORE the 3D geometry: it is a 2D pass that writes no depth, so
 * the geometry (depth-tested against the cleared far plane) draws over it. No-op guard is the caller's. */
static void DrawSky(WorldRenderer* self, World* world, const GameCamera* camera)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_skyShader);
    const WorldEnvironment* Environment = World_GetEnvironment(world);

    // Reconstruct the same view-projection BeginMode3D uses, so the sky ray matches the geometry per pixel.
    Camera3D RayCam = GameCamera_ToRaylibCamera(camera);
    double Aspect = (self->_sceneHeight > 0) ? ((double)self->_sceneWidth / (double)self->_sceneHeight) : 1.0;
    Matrix View = GetCameraMatrix(RayCam);
    Matrix Projection = MatrixPerspective((double)RayCam.fovy * DEG2RAD, Aspect, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    Matrix InvViewProj = MatrixInvert(MatrixMultiply(View, Projection));

    float Resolution[2] = { (float)self->_sceneWidth, (float)self->_sceneHeight };
    SetShaderValue(RayShader, self->_skyLocResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(RayShader, self->_skyLocInvViewProj, InvViewProj);

    float CameraPos[3] = { camera->Position.x, camera->Position.y, camera->Position.z };
    SetShaderValue(RayShader, self->_skyLocCameraPos, CameraPos, SHADER_UNIFORM_VEC3);

    Vector3 SunDirection = WorldEnvironment_GetSunDirection(Environment);
    float SunDir[3] = { SunDirection.x, SunDirection.y, SunDirection.z };
    SetShaderValue(RayShader, self->_skyLocSunDirection, SunDir, SHADER_UNIFORM_VEC3);

    float SunColor[3];
    ColorToLinear(Environment->SunColor, SunColor);
    SetShaderValue(RayShader, self->_skyLocSunColor, SunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_skyLocSunIntensity, &Environment->SunIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_skyLocSunSize, &Environment->SunSizeMultiplier, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_skyLocTurbidity, &Environment->SkyTurbidity, SHADER_UNIFORM_FLOAT);

    float SkyTint[3];
    ColorToLinear(Environment->SkyTint, SkyTint);
    SetShaderValue(RayShader, self->_skyLocSkyTint, SkyTint, SHADER_UNIFORM_VEC3);

    // The low 24 bits of the seed are enough to vary star placement and stay exactly representable as a float.
    float StarSeed = (float)(Environment->StarSeed & 0xFFFFFFu);
    SetShaderValue(RayShader, self->_skyLocStarSeed, &StarSeed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_skyLocStarDensity, &Environment->StarDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_skyLocStarBrightness, &Environment->StarBrightness, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(RayShader);
    DrawRectangle(0, 0, self->_sceneWidth, self->_sceneHeight, WHITE);
    EndShaderMode();
}

/* Draws the world's 3D content into the currently active render target with the given camera. @p modelFilter
 * selects which model objects to draw (the pixelated scene pass draws only pixelated objects when the crisp
 * overlay is active, so the flagged ones are not also drawn pixelated under the crisp layer). */
static void DrawScene(WorldRenderer* self, World* world, const GameCamera* camera, ModelPixelationFilter modelFilter)
{
    // Clear the depth buffer (and colour). When the atmospheric sky shader is present it overwrites every
    // pixel below, so the clear colour only matters as the fallback (the linearized CPU gradient) for when
    // the sky shader is unavailable. The scene buffer is linear HDR; the tonemap pass maps it to display.
    Color ClearColor = (self->_skyShader != NULL)
        ? BLACK
        : LinearizeColorBytes(WorldEnvironment_ComputeSkyColor(World_GetEnvironment(world)));
    ClearBackground(ClearColor);

    // Atmospheric sky behind the geometry (full-screen linear-HDR pass; writes no depth).
    if (self->_skyShader != NULL)
    {
        DrawSky(self, world, camera);
    }

    // Feed the sun/ambient/camera into the PBR shader before drawing any lit geometry (no-op if unavailable).
    UpdateLightingUniforms(self, world, camera);

    BeginMode3D(GameCamera_ToRaylibCamera(camera));

    if (self->_drawDebugGrid)
    {
        DrawGrid(DEBUG_GRID_SLICES, DEBUG_GRID_SPACING);
    }

    // Model objects, shaded through the PBR shader (or the default shader when it is unavailable).
    DrawWorldModels(self, world, self->_pbrShader, modelFilter);

    // Sprite objects (2D billboards). Drawn after models so they depth-test against them. Sprites are not
    // filtered by OmitPixelation yet (crisp sprites are a follow-up), so they always draw in this pixelated pass.
    DrawWorldSprites(self, world, camera);

    // Light gizmos: a debug authoring aid so light placement is visible (lights are otherwise invisible).
    if (self->_drawDebugGrid)
    {
        DrawLightGizmos(world);
    }

    EndMode3D();
}

/* Renders the world's model depth from the sun into the shadow map and stores the world->light-clip matrix
 * in _lightVP. The light is an orthographic camera looking from the sun toward the viewer's position, so the
 * shadowed area follows the camera. Opens/closes its own texture + 3D passes. */
static void RenderShadowMap(WorldRenderer* self, World* world, const GameCamera* camera, Vector3 sunDir)
{
    Vector3 Focus = camera->Position;
    Vector3 LightPos = Vector3Add(Focus, Vector3Scale(sunDir, SHADOW_DISTANCE));
    // Avoid a degenerate look-at when the sun is near straight up.
    Vector3 Up = (fabsf(sunDir.y) > 0.99f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };

    // Explicit tight orthographic frustum around the focus (rather than BeginMode3D, which would force
    // Raylib's global cull distances and make the depth bias unintuitive). Depth is linear across
    // SHADOW_NEAR..SHADOW_FAR, so SHADOW_BIAS maps to a small, world-relative offset.
    Matrix LightView = MatrixLookAt(LightPos, Focus, Up);
    float HalfExtent = SHADOW_ORTHO_SIZE*0.5f;
    Matrix LightProj = MatrixOrtho(-HalfExtent, HalfExtent, -HalfExtent, HalfExtent, SHADOW_NEAR, SHADOW_FAR);

    BeginTextureMode(self->_shadowMap);
    ClearBackground(WHITE); // clears depth to the far plane (colour is irrelevant on the depth-only target)
    rlEnableDepthTest();
    rlSetMatrixProjection(LightProj);
    rlSetMatrixModelview(LightView);   // DrawMesh reads these to build each model's light-space MVP
    // Render BACK faces into the shadow map (cull front). The lit front faces then sit well in front of the
    // stored depth, which removes most self-shadowing acne without a large (peter-panning) depth bias.
    rlSetCullFace(RL_CULL_FACE_FRONT);
    DrawWorldModels(self, world, self->_depthShader, ModelPixelationFilter_All); // everything casts shadows
    rlSetCullFace(RL_CULL_FACE_BACK);  // restore the default culling for the scene pass
    rlDisableDepthTest();
    EndTextureMode();

    self->_lightVP = MatrixMultiply(LightView, LightProj);
}

/* Runs the post-process pass (screen-space AO + hand-drawn outlines) into the currently bound post target,
 * reading the scene colour + depth. Reconstructs the same perspective projection the scene pass used so the
 * shader can turn depth back into view positions. Outputs linear HDR; the tonemap pass maps it afterwards. */
static void DrawPostFX(WorldRenderer* self, World* world, const GameCamera* camera)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_postfxShader);
    const WorldEnvironment* Environment = World_GetEnvironment(world);

    Camera3D RayCam = GameCamera_ToRaylibCamera(camera);
    double Aspect = (self->_sceneHeight > 0) ? ((double)self->_sceneWidth / (double)self->_sceneHeight) : 1.0;
    Matrix Projection = MatrixPerspective((double)RayCam.fovy * DEG2RAD, Aspect, SCENE_NEAR_PLANE, SCENE_FAR_PLANE);
    Matrix InvProjection = MatrixInvert(Projection);

    float Resolution[2] = { (float)self->_sceneWidth, (float)self->_sceneHeight };
    SetShaderValue(RayShader, self->_postfxLocResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(RayShader, self->_postfxLocProjection, Projection);
    SetShaderValueMatrix(RayShader, self->_postfxLocInvProjection, InvProjection);

    // Sun direction in VIEW space for the sun-aware crease colouring. The G-buffer stores view-space normals,
    // so transforming the (world-space) sun direction by the same view matrix's rotation makes the shader's
    // sun test a plain dot product. Subtract the transformed origin to drop the translation (a direction, not
    // a point).
    Matrix View = GetCameraMatrix(RayCam);
    Vector3 WorldSun = WorldEnvironment_GetSunDirection(Environment);
    Vector3 ViewSun = Vector3Normalize(Vector3Subtract(
        Vector3Transform(WorldSun, View), Vector3Transform((Vector3){ 0.0f, 0.0f, 0.0f }, View)));
    float SunDirView[3] = { ViewSun.x, ViewSun.y, ViewSun.z };
    SetShaderValue(RayShader, self->_postfxLocSunDirectionView, SunDirView, SHADER_UNIFORM_VEC3);

    // Effective AO strength = world multiplier x config multiplier (0 / disabled => the shader skips SSAO).
    float AoStrength = Environment->IsAmbientOcclusionEnabled
        ? (Environment->AmbientOcclusionStrength * ConfigAoStrength(self)) : 0.0f;
    if (AoStrength < 0.0f) { AoStrength = 0.0f; }
    SetShaderValue(RayShader, self->_postfxLocAoStrength, &AoStrength, SHADER_UNIFORM_FLOAT);

    // Outlines are code-configurable (not JSON) per spec: a single renderer toggle + a built-in strength.
    float OutlineStrength = self->_outlineEnabled ? OUTLINE_BASE_STRENGTH : 0.0f;
    SetShaderValue(RayShader, self->_postfxLocOutlineStrength, &OutlineStrength, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(RayShader);
    // Bind the scene depth + normal G-buffer as extra sampler2Ds. This MUST use SetShaderValueTexture (not
    // manual rlActiveTextureSlot/rlEnableTexture): DrawTexturePro draws through Raylib's 2D batch, and the
    // batch only binds textures it tracks in its own activeTextureId[] table (which SetShaderValueTexture
    // populates) plus texture0. A manual glActiveTexture bind is NOT in that table, so the batch would leave
    // these samplers on unit 0 = the scene colour texture, making the shader read colour as depth+normal (the
    // sky-noise/ring bug). Call these AFTER BeginShaderMode (its batch flush clears the table), before the draw.
    SetShaderValueTexture(RayShader, self->_postfxLocDepthTexture, self->_sceneTarget.depth);
    SetShaderValueTexture(RayShader, self->_postfxLocNormalTexture, self->_normalTarget.texture);

    // The shader samples by gl_FragCoord (not fragTexCoord), so this straight full-screen copy needs no flip.
    // DrawTexturePro binds the scene colour to unit 0 (texture0); the batch binds depth/normal to units 1/2 and
    // resets the table after drawing, so nothing lingers on those units into the next pass.
    Rectangle Source = { 0.0f, 0.0f, (float)self->_sceneTarget.texture.width, (float)self->_sceneTarget.texture.height };
    Rectangle Destination = { 0.0f, 0.0f, (float)self->_sceneWidth, (float)self->_sceneHeight };
    DrawTexturePro(self->_sceneTarget.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
}

/* Runs the bloom pass: bright-passes @p source (the linear-HDR scene, or post-processed scene) into the
 * reduced-res bloom buffer A, then blurs it with a separable Gaussian (BLOOM_BLUR_ITERATIONS horizontal+vertical
 * iterations, ping-ponging A<->B), leaving the final blurred bloom in _bloomTargetA for the tonemap to add. Each
 * sub-pass opens/closes its own texture + shader modes. The bloom shaders sample by gl_FragCoord/resolution, so
 * a positive-height full copy keeps every bloom buffer in the scene's orientation (the tonemap then samples the
 * bloom with the same fragTexCoord it uses for the scene, so the glow lines up). Caller ensures bloom is ready. */
static void RenderBloom(WorldRenderer* self, RenderTexture2D source)
{
    float BloomResolution[2] = { (float)self->_bloomWidth, (float)self->_bloomHeight };
    Rectangle FullDest = { 0.0f, 0.0f, (float)self->_bloomWidth, (float)self->_bloomHeight };
    Rectangle BloomSourceRect = { 0.0f, 0.0f, (float)self->_bloomWidth, (float)self->_bloomHeight };

    // Bright-pass + downsample the source into bloom buffer A. The sky (sun disc + stars) is excluded from bloom
    // by masking against the scene depth (sky wrote no depth → sits at the far plane), so only objects glow —
    // but only when the scene depth is a samplable texture; otherwise everything blooms (graceful fallback).
    Shader Prefilter = GameShader_GetRaylibShader(self->_bloomPrefilterShader);
    float Threshold = BLOOM_THRESHOLD;
    float SoftKnee = BLOOM_SOFT_KNEE;
    float DepthMask = self->_sceneDepthSamplable ? 1.0f : 0.0f;
    Rectangle SourceRect = { 0.0f, 0.0f, (float)source.texture.width, (float)source.texture.height };
    BeginTextureMode(self->_bloomTargetA);
    ClearBackground(BLANK);
    SetShaderValue(Prefilter, self->_bloomPrefilterLocResolution, BloomResolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(Prefilter, self->_bloomPrefilterLocThreshold, &Threshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Prefilter, self->_bloomPrefilterLocSoftKnee, &SoftKnee, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Prefilter, self->_bloomPrefilterLocDepthMask, &DepthMask, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(Prefilter);
    // Bind the scene depth (unit 1) via SetShaderValueTexture AFTER BeginShaderMode so it enters Raylib's 2D
    // batch texture table (a manual slot bind would leave the sampler on unit 0 = the scene colour). Only when
    // it will actually be sampled (depthMask on), so a disabled mask never reads an unbound sampler.
    if (self->_sceneDepthSamplable)
    {
        SetShaderValueTexture(Prefilter, self->_bloomPrefilterLocDepthTexture, self->_sceneTarget.depth);
    }
    DrawTexturePro(source.texture, SourceRect, FullDest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();

    // Separable Gaussian blur, ping-ponging A<->B: horizontal (A->B) then vertical (B->A) per iteration.
    Shader Blur = GameShader_GetRaylibShader(self->_bloomBlurShader);
    for (int Iteration = 0; Iteration < BLOOM_BLUR_ITERATIONS; Iteration++)
    {
        float DirectionH[2] = { 1.0f, 0.0f };
        BeginTextureMode(self->_bloomTargetB);
        ClearBackground(BLANK);
        SetShaderValue(Blur, self->_bloomBlurLocResolution, BloomResolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(Blur, self->_bloomBlurLocDirection, DirectionH, SHADER_UNIFORM_VEC2);
        BeginShaderMode(Blur);
        DrawTexturePro(self->_bloomTargetA.texture, BloomSourceRect, FullDest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();

        float DirectionV[2] = { 0.0f, 1.0f };
        BeginTextureMode(self->_bloomTargetA);
        ClearBackground(BLANK);
        SetShaderValue(Blur, self->_bloomBlurLocResolution, BloomResolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(Blur, self->_bloomBlurLocDirection, DirectionV, SHADER_UNIFORM_VEC2);
        BeginShaderMode(Blur);
        DrawTexturePro(self->_bloomTargetB.texture, BloomSourceRect, FullDest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();
    }
}

/* Projects the sun to a screen-space UV for the sun-shaft pass, using the same view+projection the scene pass
 * uses (so the UV lines up with the shaft buffer, which samples by gl_FragCoord/resolution → y up, matching the
 * NDC->UV here). Returns false (shafts skipped) when the sun is below SUNSHAFT_MIN_ELEVATION, behind the camera,
 * or further than SUNSHAFT_SCREEN_MARGIN outside the screen box. On success @p outUV is the sun UV in [0,1]. */
static bool ComputeSunScreenUV(const GameCamera* camera, int width, int height, Vector3 sunDir, Vector2* outUV)
{
    if (sunDir.y < SUNSHAFT_MIN_ELEVATION)
    {
        return false; // sun set / too low
    }

    Camera3D RayCam = GameCamera_ToRaylibCamera(camera);
    double Aspect = (height > 0) ? ((double)width / (double)height) : 1.0;
    Matrix View = GetCameraMatrix(RayCam);
    Matrix Projection = MatrixPerspective((double)RayCam.fovy * DEG2RAD, Aspect, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    Matrix ViewProj = MatrixMultiply(View, Projection); // world -> clip (matches DrawSky's invViewProj inverse)

    // A point far along the direction TO the sun from the camera; project it to clip space.
    Vector3 SunPoint = Vector3Add(camera->Position, Vector3Scale(sunDir, 100000.0f));
    float ClipX = ViewProj.m0*SunPoint.x + ViewProj.m4*SunPoint.y + ViewProj.m8*SunPoint.z + ViewProj.m12;
    float ClipY = ViewProj.m1*SunPoint.x + ViewProj.m5*SunPoint.y + ViewProj.m9*SunPoint.z + ViewProj.m13;
    float ClipW = ViewProj.m3*SunPoint.x + ViewProj.m7*SunPoint.y + ViewProj.m11*SunPoint.z + ViewProj.m15;
    if (ClipW <= 1e-4f)
    {
        return false; // sun behind the camera
    }

    float UvX = (ClipX / ClipW)*0.5f + 0.5f;
    float UvY = (ClipY / ClipW)*0.5f + 0.5f;
    if ((UvX < -SUNSHAFT_SCREEN_MARGIN) || (UvX > 1.0f + SUNSHAFT_SCREEN_MARGIN)
        || (UvY < -SUNSHAFT_SCREEN_MARGIN) || (UvY > 1.0f + SUNSHAFT_SCREEN_MARGIN))
    {
        return false; // sun too far off-screen for its shafts to reach the frame
    }

    outUV->x = UvX;
    outUV->y = UvY;
    return true;
}

/* Runs the sun-shaft pass: a radial blur from @p sunUV that accumulates the unoccluded sky's radiance (in its
 * own sky colour) into _sunshaftTarget; the tonemap adds it in HDR. Reads @p source's colour and the scene
 * depth (for occlusion). Opens/closes its own passes. Caller ensures shafts are ready + the sun is visible +
 * the scene depth is samplable. */
static void RenderSunshafts(WorldRenderer* self, RenderTexture2D source, Vector2 sunUV)
{
    Shader Shaft = GameShader_GetRaylibShader(self->_sunshaftShader);
    float Resolution[2] = { (float)self->_sunshaftWidth, (float)self->_sunshaftHeight };
    float SunPos[2] = { sunUV.x, sunUV.y };
    Rectangle SourceRect = { 0.0f, 0.0f, (float)source.texture.width, (float)source.texture.height };
    Rectangle FullDest = { 0.0f, 0.0f, (float)self->_sunshaftWidth, (float)self->_sunshaftHeight };

    BeginTextureMode(self->_sunshaftTarget);
    ClearBackground(BLANK);
    SetShaderValue(Shaft, self->_sunshaftLocResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(Shaft, self->_sunshaftLocSunScreenPos, SunPos, SHADER_UNIFORM_VEC2);
    BeginShaderMode(Shaft);
    // Bind the scene depth (unit 1) via SetShaderValueTexture AFTER BeginShaderMode (batch texture table), like
    // the postfx/bloom passes; the source colour is bound to unit 0 by DrawTexturePro.
    SetShaderValueTexture(Shaft, self->_sunshaftLocDepthTexture, self->_sceneTarget.depth);
    DrawTexturePro(source.texture, SourceRect, FullDest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}

/* Whether the world contains any OmitPixelation (crisp) model object; used to skip the whole crisp overlay when
 * nothing needs it. */
static bool WorldHasCrispObjects(World* world)
{
    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }
        if ((WorldObject_GetType(Object) == WorldObjectType_Model) && ((WorldModelObject*)Object)->OmitPixelation)
        {
            return true;
        }
    }
    return false;
}

/* Renders the OmitPixelation (crisp) model objects into the full-res HDR crisp target with the scene camera +
 * PBR lighting, so they can be composited un-pixelated over the pixelated frame. Opens/closes its own passes.
 * Caller ensures the crisp target + PBR shader exist. */
static void RenderCrispObjects(WorldRenderer* self, World* world, const GameCamera* camera)
{
    BeginTextureMode(self->_crispTarget);
    ClearBackground(BLANK); // clears colour to (0,0,0,0) and depth to the far plane: "no crisp object here"
    // Re-upload the PBR lighting (sun/ambient/fog/shadow + re-bind the shadow map) — the crisp pass runs after
    // the bloom/shaft passes, which may have changed GL texture-slot / shader state.
    UpdateLightingUniforms(self, world, camera);
    BeginMode3D(GameCamera_ToRaylibCamera(camera));
    DrawWorldModels(self, world, self->_pbrShader, ModelPixelationFilter_CrispOnly);
    EndMode3D();
    EndTextureMode();
}

/* Composites the crisp objects over the frame target: draws the crisp HDR colour through the crisp-composite
 * shader, which tonemaps it (matching the main tonemap, via @p exposure) and keeps only pixels where a crisp
 * object is the frontmost surface (depth-tested against the low-res scene depth). Must run into @p target AFTER
 * the pixelated scene has been blitted there. The negative source height applies the same vertical flip the
 * tonemap blit uses, so the crisp overlay aligns with the pixelated frame already in the target. */
static void CompositeCrispObjects(WorldRenderer* self, RenderTexture2D target, int windowWidth, int windowHeight,
    float exposure)
{
    Shader Composite = GameShader_GetRaylibShader(self->_crispCompositeShader);
    BeginTextureMode(target);
    SetShaderValue(Composite, self->_crispLocExposure, &exposure, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(Composite);
    // Crisp colour is bound to unit 0 by DrawTexturePro; the two depth textures go to units 1/2 via
    // SetShaderValueTexture (after BeginShaderMode) so they enter Raylib's 2D batch texture table.
    SetShaderValueTexture(Composite, self->_crispLocCrispDepth, self->_crispTarget.depth);
    SetShaderValueTexture(Composite, self->_crispLocSceneDepth, self->_sceneTarget.depth);
    Rectangle Source =
    {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)self->_crispTarget.texture.width,
        .height = -(float)self->_crispTarget.texture.height
    };
    Rectangle Destination = { .x = 0.0f, .y = 0.0f, .width = (float)windowWidth, .height = (float)windowHeight };
    DrawTexturePro(self->_crispTarget.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
}


// Public functions.
Error WorldRenderer_Create(AssetManager* assetManager, Logger* logger, const GameConfig* config,
    WorldRenderer** outRenderer)
{
    if ((assetManager == NULL) || (outRenderer == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldRenderer_Create: assetManager and outRenderer must not be NULL.");
    }
    *outRenderer = NULL;

    AssetUserID User = ASSET_USER_ID_INVALID;
    Error UserResult = AssetManager_GetNewUserID(assetManager, &User);
    if (UserResult.Code != ErrorCode_Success)
    {
        return UserResult;
    }

    WorldRenderer* Renderer = Memory_Allocate(sizeof(WorldRenderer));
    Memory_Zero(Renderer, sizeof(*Renderer));
    Renderer->_assetManager = assetManager;
    Renderer->_logger = logger;
    Renderer->_config = config;
    Renderer->_assetUser = User;
    Renderer->_drawDebugGrid = true;
    Renderer->_pixelationEnabled = true;
    Renderer->_outlineEnabled = true;
    Renderer->_postfxEnabled = true;
    Renderer->_bloomEnabled = true;
    Renderer->_sunshaftEnabled = true;
    Renderer->_crispEnabled = true;
    Renderer->_hasSceneTarget = false;

    *outRenderer = Renderer;
    return Error_CreateSuccess();
}

Error WorldRenderer_Deconstruct(WorldRenderer* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    if (self->_hasSceneTarget)
    {
        UnloadRenderTexture(self->_sceneTarget);
        self->_hasSceneTarget = false;
    }

    if (self->_hasPostTarget)
    {
        UnloadRenderTexture(self->_postTarget);
        self->_hasPostTarget = false;
    }

    if (self->_hasNormalTarget)
    {
        UnloadRenderTexture(self->_normalTarget);
        self->_hasNormalTarget = false;
    }

    if (self->_hasBloomTargets)
    {
        UnloadRenderTexture(self->_bloomTargetA);
        UnloadRenderTexture(self->_bloomTargetB);
        self->_hasBloomTargets = false;
    }

    if (self->_hasSunshaftTarget)
    {
        UnloadRenderTexture(self->_sunshaftTarget);
        self->_hasSunshaftTarget = false;
    }

    if (self->_hasCrispTarget)
    {
        UnloadRenderTexture(self->_crispTarget);
        self->_hasCrispTarget = false;
    }

    if (self->_hasShadowMap)
    {
        // Frees the framebuffer and its depth-texture attachment (Raylib queries + deletes attachments).
        rlUnloadFramebuffer(self->_shadowMap.id);
        self->_hasShadowMap = false;
    }

    Error Result = AssetManager_ReleaseAllAssetsForUser(self->_assetManager, self->_assetUser);

    Error RetireResult = AssetManager_RetireUser(self->_assetManager, self->_assetUser);
    if (Result.Code == ErrorCode_Success)
    {
        Result = RetireResult;
    }
    else
    {
        Error_Deconstruct(&RetireResult);
    }

    Memory_Free(self);
    return Result;
}

Error WorldRenderer_PrepareWorld(WorldRenderer* self, World* world)
{
    if ((self == NULL) || (world == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldRenderer_PrepareWorld: self and world must not be NULL.");
    }

    // Preload the shaders so the first frame does not stall compiling them (idempotent, logged on failure).
    EnsureShaders(self);

    size_t Count = World_GetObjectCount(world);
    for (size_t Index = 0; Index < Count; Index++)
    {
        WorldObject* Object = NULL;
        Error GetResult = World_GetObjectByIndex(world, Index, &Object);
        if (GetResult.Code != ErrorCode_Success)
        {
            Error_Deconstruct(&GetResult);
            continue;
        }

        WorldObjectType Type = WorldObject_GetType(Object);
        if (Type == WorldObjectType_Model)
        {
            const unsigned char* AssetName = WorldModelObject_GetModelAssetName((WorldModelObject*)Object);
            if (AssetName == NULL)
            {
                continue;
            }
            GameModel* ModelAsset = NULL;
            Error LoadResult = AssetManager_LoadModel(self->_assetManager, AssetName, self->_assetUser, &ModelAsset);
            if (LoadResult.Code != ErrorCode_Success)
            {
                ReportAssetFailure(self, AssetName, &LoadResult);
            }
        }
        else if (Type == WorldObjectType_Sprite)
        {
            const unsigned char* AssetName = WorldSpriteObject_GetSpriteAnimationAssetName((WorldSpriteObject*)Object);
            if (AssetName == NULL)
            {
                continue;
            }
            SpriteAnimation* Animation = NULL;
            Error LoadResult = AssetManager_LoadSpriteAnimation(self->_assetManager, AssetName, self->_assetUser, &Animation);
            if (LoadResult.Code != ErrorCode_Success)
            {
                ReportAssetFailure(self, AssetName, &LoadResult);
            }
        }
    }

    return Error_CreateSuccess();
}

Error WorldRenderer_RenderToTarget(WorldRenderer* self, World* world, const GameCamera* camera,
    double deltaSeconds, RenderTexture2D target)
{
    if ((self == NULL) || (world == NULL) || (camera == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "WorldRenderer_RenderToTarget: self, world and camera must not be NULL.");
    }

    EnsureShaders(self);

    // Decide whether the sun casts shadows this frame; if so, render the shadow map BEFORE the scene pass.
    const WorldEnvironment* Environment = World_GetEnvironment(world);
    Vector3 SunDir = WorldEnvironment_GetSunDirection(Environment);
    self->_shadowActive = self->_hasShadowMap && (self->_depthShader != NULL) && (self->_pbrShader != NULL)
        && Environment->AreShadowsEnabled && ((Environment->ShadowStrength * ConfigShadowStrength(self)) > 0.0f)
        && (SunDir.y > SHADOW_MIN_SUN_ELEVATION);
    if (self->_shadowActive)
    {
        RenderShadowMap(self, world, camera, SunDir);
    }

    int WindowWidth = target.texture.width;
    int WindowHeight = target.texture.height;
    EnsureSceneTarget(self, WindowWidth, WindowHeight);

    // Decide whether the crisp overlay runs this frame (un-pixelated OmitPixelation objects composited full-res
    // over the pixelated frame). When it does, the pixelated scene + normal passes SKIP the flagged objects (the
    // crisp pass draws them instead); when it does not, the flagged objects fall back to the pixelated pass so
    // they never vanish. Needs the crisp shader + a samplable-depth crisp target + samplable scene depth (for the
    // occlusion test) + at least one flagged object.
    bool CrispActive = self->_crispEnabled && (self->_crispCompositeShader != NULL) && (self->_pbrShader != NULL)
        && self->_hasCrispTarget && self->_crispDepthSamplable && self->_sceneDepthSamplable
        && WorldHasCrispObjects(world);
    ModelPixelationFilter SceneFilter = CrispActive
        ? ModelPixelationFilter_PixelatedOnly : ModelPixelationFilter_All;

    // Pass 1: draw the 3D world into the scene target (pixel resolution when pixelating).
    BeginTextureMode(self->_sceneTarget);
    DrawScene(self, world, camera, SceneFilter);
    EndTextureMode();

    // Pass 1.5 (optional): screen-space AO + depth/normal-edge outlines, low-res, reading the scene colour +
    // depth + a normal G-buffer. Needs the master toggle on, at least one contributing effect, the postfx +
    // normal shaders, the samplable-depth HDR scene target, and both post targets; otherwise the scene is
    // blitted as-is (bit-exact with the pre-postfx pipeline). The normal pass runs first so postfx can read it.
    RenderTexture2D BlitSource = self->_sceneTarget;
    float EffectiveAoStrength = Environment->IsAmbientOcclusionEnabled
        ? (Environment->AmbientOcclusionStrength * ConfigAoStrength(self)) : 0.0f;
    if (self->_postfxEnabled && ((EffectiveAoStrength > 0.0f) || self->_outlineEnabled)
        && (self->_postfxShader != NULL) && (self->_normalShader != NULL) && self->_hasPostTarget
        && self->_hasNormalTarget && self->_sceneDepthSamplable)
    {
        RenderNormalBuffer(self, world, camera, SceneFilter);
        BeginTextureMode(self->_postTarget);
        ClearBackground(BLACK);
        DrawPostFX(self, world, camera);
        EndTextureMode();
        BlitSource = self->_postTarget;
    }

    // Pass 1.75 (optional): bloom. Bright-pass + blur the (post-processed) HDR scene into _bloomTargetA; the
    // tonemap adds it back in linear HDR. Needs bloom on (code toggle + world flag), a positive effective
    // strength (world x config), the bloom shaders/targets, and the tonemap's bloom uniforms.
    float EffectiveBloom = (Environment->IsBloomEnabled && self->_bloomEnabled)
        ? (Environment->BloomStrength * ConfigBloomStrength(self) * BLOOM_BASE_INTENSITY) : 0.0f;
    if (EffectiveBloom < 0.0f) { EffectiveBloom = 0.0f; }
    bool BloomActive = (EffectiveBloom > 0.0f) && self->_hasBloomTargets
        && (self->_bloomPrefilterShader != NULL) && (self->_bloomBlurShader != NULL)
        && (self->_tonemapShader != NULL) && (self->_tonemapLocBloomStrength >= 0);
    if (BloomActive)
    {
        RenderBloom(self, BlitSource);
    }

    // Pass 1.8 (optional): sun shafts (god rays). Needs shafts on (code toggle + world flag), a positive
    // effective strength (world x config), the shaft shader/target, the tonemap's shaft uniforms, samplable
    // scene depth (for occlusion), AND the sun projecting to a usable on-screen position (above the horizon,
    // in front, not too far off-screen). The shaft radiance is tinted by the (linear) sun colour.
    float EffectiveSunshaft = (Environment->AreSunshaftsEnabled && self->_sunshaftEnabled)
        ? (Environment->SunshaftStrength * ConfigSunshaftStrength(self) * SUNSHAFT_BASE_INTENSITY) : 0.0f;
    if (EffectiveSunshaft < 0.0f) { EffectiveSunshaft = 0.0f; }
    Vector2 SunUV = { 0.0f, 0.0f };
    bool SunshaftActive = (EffectiveSunshaft > 0.0f) && self->_hasSunshaftTarget && (self->_sunshaftShader != NULL)
        && (self->_tonemapShader != NULL) && (self->_tonemapLocSunshaftStrength >= 0) && self->_sceneDepthSamplable
        && ComputeSunScreenUV(camera, self->_sceneWidth, self->_sceneHeight, SunDir, &SunUV);
    if (SunshaftActive)
    {
        RenderSunshafts(self, BlitSource, SunUV);
    }

    // Pass 2: blit the (post-processed) scene into the frame target, tonemapping the linear-HDR result to
    // sRGB via the tonemap shader as it upscales. A negative source height applies the vertical flip Raylib
    // render textures need (the same convention the frame manager uses when compositing), and point filtering
    // on the scene texture makes the upscale produce hard square pixels. If the tonemap shader is unavailable
    // the raw (untonemapped) HDR is blitted so the scene still shows (degraded).
    BeginTextureMode(target);
    ClearBackground(BLACK);
    Rectangle Source =
    {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)BlitSource.texture.width,
        .height = -(float)BlitSource.texture.height
    };
    Rectangle Destination = { .x = 0.0f, .y = 0.0f, .width = (float)WindowWidth, .height = (float)WindowHeight };
    // HDR eye adaptation: ease the exposure toward the scene brightness and feed it to the tonemap, so the
    // day-night cycle stays readable (a dark night is lifted; a bright light pulls it back down).
    float Exposure = UpdateEyeAdaptation(self, world, camera, (float)deltaSeconds);
    if (self->_tonemapShader != NULL)
    {
        Shader TonemapRay = GameShader_GetRaylibShader(self->_tonemapShader);
        SetShaderValue(TonemapRay, self->_tonemapLocExposure, &Exposure, SHADER_UNIFORM_FLOAT);
        // Bloom + sun-shaft strengths (0 when inactive → the shader skips sampling that texture, so it need not
        // bind). Both are added in linear HDR before the tonemap.
        float BloomStrengthUniform = BloomActive ? EffectiveBloom : 0.0f;
        SetShaderValue(TonemapRay, self->_tonemapLocBloomStrength, &BloomStrengthUniform, SHADER_UNIFORM_FLOAT);
        float SunshaftStrengthUniform = SunshaftActive ? EffectiveSunshaft : 0.0f;
        SetShaderValue(TonemapRay, self->_tonemapLocSunshaftStrength, &SunshaftStrengthUniform, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(TonemapRay);
        // Bind the extra samplers (bloom unit 1, sun shafts unit 2) AFTER BeginShaderMode, via SetShaderValueTexture
        // so they enter Raylib's 2D batch texture table (the manual-slot idiom would leave them on unit 0 = scene).
        if (BloomActive)
        {
            SetShaderValueTexture(TonemapRay, self->_tonemapLocBloomTexture, self->_bloomTargetA.texture);
        }
        if (SunshaftActive)
        {
            SetShaderValueTexture(TonemapRay, self->_tonemapLocSunshaftTexture, self->_sunshaftTarget.texture);
        }
    }
    DrawTexturePro(BlitSource.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    if (self->_tonemapShader != NULL)
    {
        EndShaderMode();
    }
    EndTextureMode();

    // Pass 3 (optional): crisp overlay. Render the un-pixelated OmitPixelation objects at full window resolution
    // and composite them over the just-blitted pixelated frame, depth-tested against the scene so they are
    // occluded correctly. Runs last so it draws over the finished pixelated image (e.g. a readable 3D screen).
    if (CrispActive)
    {
        RenderCrispObjects(self, world, camera);
        CompositeCrispObjects(self, target, WindowWidth, WindowHeight, Exposure);
    }

    return Error_CreateSuccess();
}

void WorldRenderer_SetPixelationEnabled(WorldRenderer* self, bool enabled)
{
    self->_pixelationEnabled = enabled;
}

bool WorldRenderer_IsPixelationEnabled(const WorldRenderer* self)
{
    return self->_pixelationEnabled;
}

void WorldRenderer_SetPostEffectsEnabled(WorldRenderer* self, bool enabled)
{
    self->_postfxEnabled = enabled;
}

bool WorldRenderer_ArePostEffectsEnabled(const WorldRenderer* self)
{
    return self->_postfxEnabled;
}

void WorldRenderer_SetBloomEnabled(WorldRenderer* self, bool enabled)
{
    self->_bloomEnabled = enabled;
}

bool WorldRenderer_IsBloomEnabled(const WorldRenderer* self)
{
    return self->_bloomEnabled;
}

void WorldRenderer_SetSunshaftsEnabled(WorldRenderer* self, bool enabled)
{
    self->_sunshaftEnabled = enabled;
}

bool WorldRenderer_AreSunshaftsEnabled(const WorldRenderer* self)
{
    return self->_sunshaftEnabled;
}

void WorldRenderer_SetCrispOverlayEnabled(WorldRenderer* self, bool enabled)
{
    self->_crispEnabled = enabled;
}

bool WorldRenderer_IsCrispOverlayEnabled(const WorldRenderer* self)
{
    return self->_crispEnabled;
}

void WorldRenderer_SetDebugGridEnabled(WorldRenderer* self, bool enabled)
{
    self->_drawDebugGrid = enabled;
}

bool WorldRenderer_IsDebugGridEnabled(const WorldRenderer* self)
{
    return self->_drawDebugGrid;
}
