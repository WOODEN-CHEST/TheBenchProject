#include <math.h>
#include "WorldRenderer.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelObject.h"
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


// Types.
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

    if (self->_hasSceneTarget && (self->_sceneWidth == DesiredWidth) && (self->_sceneHeight == DesiredHeight))
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

    float SunColor[3];
    ColorToLinear(Environment->SunColor, SunColor);
    SetShaderValue(RayShader, self->_locSunColor, SunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_locSunIntensity, &Environment->SunIntensity, SHADER_UNIFORM_FLOAT);

    float AmbientColor[3];
    ColorToLinear(Environment->AmbientSkylightColor, AmbientColor);
    SetShaderValue(RayShader, self->_locAmbientColor, AmbientColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_locAmbientIntensity, &Environment->AmbientSkylightIntensity, SHADER_UNIFORM_FLOAT);

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

/* Draws every model object in the world through @p shader. Sprites/lights are not drawn here yet. */
static void DrawWorldModels(WorldRenderer* self, World* world, GameShader* shader)
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
        if (WorldObject_GetType(Object) == WorldObjectType_Model)
        {
            DrawModelObjectWithShader(self, world, (WorldModelObject*)Object, shader);
        }
    }
}

/* Draws every world MODEL object into the normal G-buffer through the normal shader, setting the per-object
 * outline flag uniform (1 when the object has outlines enabled, else 0) before each draw. The normal shader
 * writes RGB=view-space normal, A=surface/outline flag (0.5 = surface, 1.0 = surface with outline); pixels the
 * sky/grid leave untouched stay at the cleared 0 (not a surface). */
static void DrawNormalBufferModels(WorldRenderer* self, World* world)
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
static void RenderNormalBuffer(WorldRenderer* self, World* world, const GameCamera* camera)
{
    BeginTextureMode(self->_normalTarget);
    ClearBackground(BLANK); // RGBA (0,0,0,0): A=0 = "not a surface" everywhere the geometry does not draw
    rlDisableColorBlend();  // write RGB (normal) + A (flag) directly; no alpha blend
    BeginMode3D(GameCamera_ToRaylibCamera(camera));
    DrawNormalBufferModels(self, world);
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

/* Draws the world's 3D content into the currently active render target with the given camera. */
static void DrawScene(WorldRenderer* self, World* world, const GameCamera* camera)
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
    // TODO: sprite objects (world-placed planes) and light objects layer in later.
    DrawWorldModels(self, world, self->_pbrShader);

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
    DrawWorldModels(self, world, self->_depthShader);
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

        if (WorldObject_GetType(Object) != WorldObjectType_Model)
        {
            continue;
        }

        WorldModelObject* ModelObject = (WorldModelObject*)Object;
        const unsigned char* AssetName = WorldModelObject_GetModelAssetName(ModelObject);
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

    return Error_CreateSuccess();
}

Error WorldRenderer_RenderToTarget(WorldRenderer* self, World* world, const GameCamera* camera, RenderTexture2D target)
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

    // Pass 1: draw the 3D world into the scene target (pixel resolution when pixelating).
    BeginTextureMode(self->_sceneTarget);
    DrawScene(self, world, camera);
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
        RenderNormalBuffer(self, world, camera);
        BeginTextureMode(self->_postTarget);
        ClearBackground(BLACK);
        DrawPostFX(self, world, camera);
        EndTextureMode();
        BlitSource = self->_postTarget;
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
    if (self->_tonemapShader != NULL)
    {
        BeginShaderMode(GameShader_GetRaylibShader(self->_tonemapShader));
    }
    DrawTexturePro(BlitSource.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    if (self->_tonemapShader != NULL)
    {
        EndShaderMode();
    }
    EndTextureMode();

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

void WorldRenderer_SetDebugGridEnabled(WorldRenderer* self, bool enabled)
{
    self->_drawDebugGrid = enabled;
}

bool WorldRenderer_IsDebugGridEnabled(const WorldRenderer* self)
{
    return self->_drawDebugGrid;
}
