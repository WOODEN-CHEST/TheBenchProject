#include <math.h>
#include "WorldRenderer.h"
#include "World.h"
#include "WorldObject.h"
#include "WorldModelObject.h"
#include "GameCamera.h"
#include "AssetManager.h"
#include "GameModel.h"
#include "GameShader.h"
#include "Logger.h"
#include "Renderer.h"
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


// Types.
struct WorldRendererStruct
{
    /** Asset manager used to resolve object assets; borrowed. */
    AssetManager* _assetManager;
    /** Logger for diagnostics; borrowed, may be NULL. */
    Logger* _logger;
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
    /** Set once the shaders' load has been attempted (success or failure), so it is tried only once. */
    bool _shadersLoadAttempted;
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
    /** Internal scene target the 3D world renders into (pixel resolution when pixelating). */
    RenderTexture2D _sceneTarget;
    /** Whether _sceneTarget has been created. */
    bool _hasSceneTarget;
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

/* Creates a scene render target whose colour attachment is a 16-bit float (HDR) texture plus a depth
 * renderbuffer, mirroring raylib's LoadRenderTexture but with a floating-point colour format so the scene
 * can hold linear HDR values before tonemapping. Falls back to a standard 8-bit target (and sets *outIsHDR
 * false) if the GPU cannot make the float framebuffer complete. */
static RenderTexture2D CreateSceneTarget(int width, int height, bool* outIsHDR)
{
    *outIsHDR = false;

    RenderTexture2D Target = { 0 };
    Target.id = rlLoadFramebuffer();
    if (Target.id == 0)
    {
        return LoadRenderTexture(width, height);
    }

    rlEnableFramebuffer(Target.id);

    Target.texture.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    Target.texture.width = width;
    Target.texture.height = height;
    Target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    Target.texture.mipmaps = 1;

    Target.depth.id = rlLoadTextureDepth(width, height, true);
    Target.depth.width = width;
    Target.depth.height = height;
    Target.depth.format = SCENE_DEPTH_FORMAT;
    Target.depth.mipmaps = 1;

    rlFramebufferAttach(Target.id, Target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(Target.id, Target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);

    bool Complete = (Target.texture.id != 0) && rlFramebufferComplete(Target.id);
    rlDisableFramebuffer();

    if (!Complete)
    {
        UnloadRenderTexture(Target); // frees the framebuffer and its attachments
        return LoadRenderTexture(width, height);
    }

    *outIsHDR = true;
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

    self->_sceneTarget = CreateSceneTarget(DesiredWidth, DesiredHeight, &self->_sceneIsHDR);
    // Point filtering makes the upscale to the window produce hard, square pixels.
    SetTextureFilter(self->_sceneTarget.texture, TEXTURE_FILTER_POINT);
    self->_hasSceneTarget = true;
    self->_sceneWidth = DesiredWidth;
    self->_sceneHeight = DesiredHeight;

    // Report the HDR/fallback choice once so it is easy to confirm the float pipeline is active.
    if (!self->_sceneHdrReported && (self->_logger != NULL))
    {
        self->_sceneHdrReported = true;
        Error LogResult = Logger_LogInfo(self->_logger, self->_sceneIsHDR
            ? (const unsigned char*)u8"WorldRenderer: scene target is HDR (RGBA16F float)."
            : (const unsigned char*)u8"WorldRenderer: HDR framebuffer unsupported; using 8-bit scene target.");
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

        // Constant material parameters (scalar until per-material PBR maps land); upload once.
        float Metallic = PBR_DEFAULT_METALLIC;
        float Roughness = PBR_DEFAULT_ROUGHNESS;
        float Ao = PBR_DEFAULT_AO;
        float EmissiveColor[3] = { 0.0f, 0.0f, 0.0f };
        float EmissiveIntensity = 0.0f;
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "metallic"), &Metallic, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "roughness"), &Roughness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "ao"), &Ao, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "emissiveColor"), EmissiveColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(RayShader, GetShaderLocation(RayShader, "emissiveIntensity"), &EmissiveIntensity, SHADER_UNIFORM_FLOAT);
    }

    self->_tonemapShader = LoadRendererShader(self, TONEMAP_SHADER_ASSET_NAME);
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
}

/* Draws a single model object. Missing/failed assets are skipped silently (PrepareWorld reports them). */
static void DrawModelObject(WorldRenderer* self, WorldModelObject* modelObject)
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

    // Bind the PBR shader onto every material slot so DrawModel shades through it. The by-value model copy
    // shares the asset's materials array, so this persists on the asset (intended: all objects shade PBR).
    // When the shader is unavailable the material keeps Raylib's default shader (unlit) so the model still draws.
    if (self->_pbrShader != NULL)
    {
        Shader PbrShader = GameShader_GetRaylibShader(self->_pbrShader);
        for (int MaterialIndex = 0; MaterialIndex < RayModel.materialCount; MaterialIndex++)
        {
            RayModel.materials[MaterialIndex].shader = PbrShader;
        }
    }

    RayModel.transform = MatrixMultiply(RayModel.transform, World);
    Color Tint = RenderColor_GetFinalColor(WorldObject_GetTint(Base));
    DrawModel(RayModel, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, Tint);
}

/* Draws the world's 3D content into the currently active render target with the given camera. */
static void DrawScene(WorldRenderer* self, World* world, const GameCamera* camera)
{
    // Clear to the world's current sky color (a time-of-day gradient). The scene buffer is linear HDR, so
    // linearize the sRGB sky before clearing; the tonemap pass maps the whole scene back to display range.
    // (The full atmospheric sky pass replaces this later, writing linear HDR directly.)
    ClearBackground(LinearizeColorBytes(WorldEnvironment_ComputeSkyColor(World_GetEnvironment(world))));

    // Feed the sun/ambient/camera into the PBR shader before drawing any lit geometry (no-op if unavailable).
    UpdateLightingUniforms(self, world, camera);

    BeginMode3D(GameCamera_ToRaylibCamera(camera));

    if (self->_drawDebugGrid)
    {
        DrawGrid(DEBUG_GRID_SLICES, DEBUG_GRID_SPACING);
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

        switch (WorldObject_GetType(Object))
        {
            case WorldObjectType_Model:
                DrawModelObject(self, (WorldModelObject*)Object);
                break;

            case WorldObjectType_Sprite:
                // TODO: draw sprite objects (textured, world-placed planes) in a later step.
                break;

            case WorldObjectType_Light:
                // TODO: feed lights into the (future) lighting pass; lights draw no geometry.
                break;
        }
    }

    EndMode3D();
}


// Public functions.
Error WorldRenderer_Create(AssetManager* assetManager, Logger* logger, WorldRenderer** outRenderer)
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
    Renderer->_assetUser = User;
    Renderer->_drawDebugGrid = true;
    Renderer->_pixelationEnabled = true;
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

    int WindowWidth = target.texture.width;
    int WindowHeight = target.texture.height;
    EnsureSceneTarget(self, WindowWidth, WindowHeight);

    // Pass 1: draw the 3D world into the scene target (pixel resolution when pixelating).
    BeginTextureMode(self->_sceneTarget);
    DrawScene(self, world, camera);
    EndTextureMode();

    // Pass 2: blit the scene target into the frame target, tonemapping the linear-HDR scene to displayable
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
        .width = (float)self->_sceneTarget.texture.width,
        .height = -(float)self->_sceneTarget.texture.height
    };
    Rectangle Destination = { .x = 0.0f, .y = 0.0f, .width = (float)WindowWidth, .height = (float)WindowHeight };
    if (self->_tonemapShader != NULL)
    {
        BeginShaderMode(GameShader_GetRaylibShader(self->_tonemapShader));
    }
    DrawTexturePro(self->_sceneTarget.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
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

void WorldRenderer_SetDebugGridEnabled(WorldRenderer* self, bool enabled)
{
    self->_drawDebugGrid = enabled;
}

bool WorldRenderer_IsDebugGridEnabled(const WorldRenderer* self)
{
    return self->_drawDebugGrid;
}
