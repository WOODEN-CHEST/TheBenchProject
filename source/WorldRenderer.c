// The world renderer's public API: creation and teardown, world preparation, and RenderToTarget, which builds the
// per-frame context, decides which passes run, and runs them in order. The passes live in WorldRenderer*.c (see
// WorldRendererInternal.h); keep this file about orchestration and keep each pass's code in its own file.

#include <inttypes.h>
#include <math.h>
#include "WorldRendererInternal.h"
#include "WorldLightCulling.h"
#include "NightSky.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "wr/WRMemory.h"


// Macros.
/** Base bloom intensity at world+config bloom strength 1.0 (how strongly the blurred bright-pass is added back
 *  in linear HDR); the world and config bloom-strength multipliers scale this. */
#define BLOOM_BASE_INTENSITY 0.75f
/** Base sun-shaft intensity at world+config strength 1.0 (how strongly the shafts are added back in linear
 *  HDR); the world and config sunshaft-strength multipliers scale this. Kept low — shafts sample the very
 *  bright near-sun sky, so a little goes a long way. */
#define SUNSHAFT_BASE_INTENSITY 0.5f
/** Initial capacity (in lights) of the forward-light lists; they grow with the world. */
#define INITIAL_FORWARD_LIGHT_CAPACITY 32
/** Initial capacity (in bytes) of the status log line. */
#define STATUS_LINE_CAPACITY 256
/** The render clock (WorldRenderFrame.TimeSeconds) wraps after this many seconds, so a float still resolves
 *  milliseconds. */
#define RENDER_TIME_WRAP_SECONDS 3600.0


// Static functions.
/* Loads every pass's shaders once (and the GPU resources that do not depend on the frame size). Every shader is
 * optional; a missing one is logged once and switches its feature off. Needs a live GL context. */
static void EnsureShaders(WorldRenderer* self)
{
    if (self->_shadersLoadAttempted)
    {
        return;
    }
    self->_shadersLoadAttempted = true;

    WorldRendererLighting_LoadShader(self);
    WorldRendererPost_LoadShaders(self);
    WorldRendererCrisp_LoadShader(self);
    WorldRendererSprites_LoadShaders(self);
    WorldRendererGBuffer_LoadShaders(self);
    WorldRendererShadows_LoadShaders(self);
    WorldRendererSky_LoadShader(self);
    WorldRendererStars_LoadShader(self);
}

// What each optional feature needs to be able to run at all (its shaders and targets), independent of whether it
// is wanted this frame.
static bool IsCascadeShadowAvailable(const WorldRenderer* self)
{
    return self->_cascades._hasNearMap && (self->_cascades._depthShader != NULL) && (self->_pbr._shader != NULL);
}

static bool IsPointShadowAvailable(const WorldRenderer* self)
{
    return self->_pointShadow._hasCube && (self->_pointShadow._shader != NULL) && (self->_pbr._shader != NULL);
}

static bool IsAoAvailable(const WorldRenderer* self)
{
    return (self->_pbr._shader != NULL) && (self->_ao._shader != NULL) && (self->_gbuffer._shader != NULL)
        && self->_gbuffer._hasTarget && self->_gbuffer._isDepthSamplable && self->_ao._hasTarget;
}

static bool AreOutlinesAvailable(const WorldRenderer* self)
{
    return (self->_outline._shader != NULL) && (self->_gbuffer._shader != NULL) && self->_outline._hasTarget
        && self->_gbuffer._hasTarget && self->_scene._isDepthSamplable;
}

static bool IsBloomAvailable(const WorldRenderer* self)
{
    return self->_bloom._hasTargets && (self->_bloom._prefilterShader != NULL) && (self->_bloom._blurShader != NULL)
        && (self->_tonemap._shader != NULL) && (self->_tonemap._locBloomStrength >= 0);
}

static bool AreSunshaftsAvailable(const WorldRenderer* self)
{
    return self->_sunshaft._hasTarget && (self->_sunshaft._shader != NULL) && (self->_tonemap._shader != NULL)
        && (self->_tonemap._locSunshaftStrength >= 0) && self->_scene._isDepthSamplable;
}

static bool IsCrispAvailable(const WorldRenderer* self)
{
    return (self->_crisp._compositeShader != NULL) && (self->_pbr._shader != NULL) && self->_crisp._hasTarget
        && self->_scene._isDepthSamplable;
}

static bool AreStarsAvailable(const WorldRenderer* self)
{
    return self->_stars._shader != NULL;
}

static bool IsMilkyWayAvailable(const WorldRenderer* self)
{
    return (self->_sky._shader != NULL) && (self->_sky._milkyWay._texture != NULL);
}

static bool IsMoonAvailable(const WorldRenderer* self)
{
    return self->_sky._shader != NULL; // without its texture the disc is a flat grey
}

static bool IsMoonlightAvailable(const WorldRenderer* self)
{
    return self->_pbr._shader != NULL;
}

static bool IsNightVisionAvailable(const WorldRenderer* self)
{
    return (self->_pbr._shader != NULL) || (self->_sky._shader != NULL); // the passes that apply it (sprites too)
}

static bool AreSpriteShadowsAvailable(const WorldRenderer* self)
{
    return WorldRendererSprites_IsAvailable(self)
        && ((IsCascadeShadowAvailable(self) && (self->_cascades._spriteDepthShader != NULL))
            || (IsPointShadowAvailable(self) && (self->_pointShadow._spriteShader != NULL)));
}

/* Whether any sprite drawn this frame casts a shadow: a shadow pass runs that has its sprite shader, and a sprite
 * casts. */
static bool DoSpritesCastShadows(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    bool IsCascadeCasting = frame->IsCascadeShadowActive && (self->_cascades._spriteDepthShader != NULL);
    bool IsCubeCasting = frame->IsPointShadowActive && (self->_pointShadow._spriteShader != NULL);
    if (!WorldRendererSprites_IsAvailable(self) || !(IsCascadeCasting || IsCubeCasting))
    {
        return false;
    }
    size_t SpriteCount = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&self->_cache, &SpriteCount);
    for (size_t Index = 0; Index < SpriteCount; Index++)
    {
        if (WorldRendererSprites_CastsShadows(&Sprites[Index]))
        {
            return true;
        }
    }
    return false;
}

/* The eye's night vision this frame: how strongly it applies (the renderer toggle x the world's strength), the field
 * luminance it compares with (the sky's light, BaseSceneLuminance, so it runs after WorldRendererLighting_PrepareFrame;
 * a non-finite one counts as daylight, changing nothing), the rods' grey (the world's blue shift), and whether it
 * changes anything: only while the field is below the full-colour luminance (by day the shaders skip it). */
static void DecideNightVision(const WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    float Strength = fminf(fmaxf(Environment->NightVisionStrength, 0.0f), 1.0f); // fmaxf drops a NaN
    frame->NightVisionStrength = self->_isNightVisionEnabled ? Strength : 0.0f;
    frame->NightVisionFieldLuminance = isfinite(frame->BaseSceneLuminance)
        ? fmaxf(frame->BaseSceneLuminance, 0.0f) : self->_nightVision.FullColourLuminance;
    NightVision_GetRodTint(Environment->NightVisionBlueShift, frame->NightVisionTint);
    frame->IsNightVisionActive = IsNightVisionAvailable(self) && (frame->NightVisionStrength > 0.0f)
        && (frame->NightVisionFieldLuminance < self->_nightVision.FullColourLuminance);
}

/* Advances the render clock behind WorldRenderFrame.TimeSeconds, wrapping it every RENDER_TIME_WRAP_SECONDS. A zero,
 * negative or non-finite delta holds it. */
static void AdvanceRenderTime(WorldRenderer* self, double deltaSeconds)
{
    if (!isfinite(deltaSeconds) || (deltaSeconds <= 0.0))
    {
        return;
    }
    self->_renderTimeSeconds = fmod(self->_renderTimeSeconds + deltaSeconds, RENDER_TIME_WRAP_SECONDS);
}

/* Fills the frame's sky from the environment's model: the sun, the moon, the two lights (their colours come with the
 * lighting) and which of them casts the shadows, and the stars' rotation. */
static void BuildSky(WorldRenderFrame* frame)
{
    WorldSkyLights Lights;
    WorldEnvironment_GetSkyLights(frame->Environment, &Lights);
    frame->SunDirection = Lights.SunDirection;
    frame->Daylight = Lights.Daylight;
    frame->MoonDirection = Lights.MoonDirection;
    frame->MoonPhase = Lights.MoonPhase;
    frame->MoonIllumination = Lights.MoonIllumination;
    frame->SunLight.Direction = Lights.SunDirection;
    frame->SunLight.Intensity = Lights.SunIntensity;
    frame->MoonLight.Direction = Lights.MoonDirection;
    frame->MoonLight.Intensity = Lights.MoonIntensity;
    frame->ShadowLight = Lights.ShadowLight;
    frame->ShadowDirection = Lights.ShadowDirection;
    frame->ShadowFade = Lights.ShadowFade;
    frame->StarsToWorld = WorldEnvironment_GetStarsToWorld(frame->Environment);
}

/* Fills the frame's world, camera and sky fields and the scene matrices: the projection BeginMode3D builds for
 * the scene target (the camera's vertical FOV, the scene aspect and raylib's cull distances), so every pass that
 * reconstructs positions from depth, or rays from pixels, matches the geometry exactly. */
static void BuildFrame(const WorldRenderer* self, World* world, const GameCamera* camera, RenderTexture2D target,
    WorldRenderFrame* outFrame)
{
    *outFrame = (WorldRenderFrame){ 0 };
    outFrame->World = world;
    outFrame->Environment = World_GetEnvironment(world);
    outFrame->Camera = camera;
    outFrame->RayCamera = GameCamera_ToRaylibCamera(camera);
    BuildSky(outFrame);
    outFrame->TimeSeconds = (float)self->_renderTimeSeconds;
    outFrame->WindowWidth = target.texture.width;
    outFrame->WindowHeight = target.texture.height;
    outFrame->SceneWidth = self->_scene._width;
    outFrame->SceneHeight = self->_scene._height;
    outFrame->SceneFilter = WorldRendererPixelFilter_All;
    outFrame->ShadowLightIndex = SIZE_MAX;

    double Aspect = (outFrame->SceneHeight > 0) ? ((double)outFrame->SceneWidth / (double)outFrame->SceneHeight) : 1.0;
    outFrame->View = GetCameraMatrix(outFrame->RayCamera);
    outFrame->Projection = MatrixPerspective((double)outFrame->RayCamera.fovy * DEG2RAD, Aspect, SCENE_NEAR_PLANE,
        SCENE_FAR_PLANE);
    outFrame->InvProjection = MatrixInvert(outFrame->Projection);
    outFrame->ViewProjection = MatrixMultiply(outFrame->View, outFrame->Projection);
    outFrame->InvViewProjection = MatrixInvert(outFrame->ViewProjection);
}

/* Decides which optional passes run this frame and with what strength: each needs to be available (its shaders
 * and targets), wanted (renderer toggle, world flag, world x config strength above 0) and applicable (a shadow light
 * high enough, something to draw). */
static void DecidePasses(WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    bool AreShadowsWanted = Environment->AreShadowsEnabled
        && ((Environment->ShadowStrength * WorldRendererCommon_ConfigShadowStrength(self)) > 0.0f);

    // The cascades follow the sky's shadow light (the sun by day, the moon on bright nights, none while both are low or
    // handing over: WorldEnvironment_GetSkyLights decides); the point-light shadow is independent of the sky (shadows
    // at night), cast by the nearest shadow-casting light. Both are rendered before the scene pass.
    frame->IsCascadeShadowActive = IsCascadeShadowAvailable(self) && AreShadowsWanted
        && (frame->ShadowLight != WorldShadowLight_None) && (frame->ShadowFade > 0.0f);
    if (IsPointShadowAvailable(self) && AreShadowsWanted)
    {
        frame->ShadowLightIndex = WorldRendererShadows_SelectPointLight(self, frame->Camera->Position);
        frame->IsPointShadowActive = (frame->ShadowLightIndex != SIZE_MAX);
    }

    // The crisp overlay draws OmitPixelation objects at window resolution over the pixelated frame. When it runs,
    // the pixelated passes skip those objects; when it cannot, they fall back to the pixelated passes so they
    // never vanish.
    frame->IsCrispActive = self->_crisp._isEnabled && IsCrispAvailable(self)
        && WorldRenderCache_HasCrispObjects(&self->_cache);
    frame->SceneFilter = frame->IsCrispActive ? WorldRendererPixelFilter_PixelatedOnly : WorldRendererPixelFilter_All;

    // AO into the ambient term and the outlines both read the G-buffer; both are under the post-effects master
    // toggle.
    float AoStrength = Environment->IsAmbientOcclusionEnabled
        ? (Environment->AmbientOcclusionStrength * WorldRendererCommon_ConfigAoStrength(self)) : 0.0f;
    frame->IsAoActive = self->_postfxEnabled && (AoStrength > 0.0f) && IsAoAvailable(self);
    frame->AoStrength = frame->IsAoActive ? AoStrength : 0.0f;
    frame->AreOutlinesActive = self->_postfxEnabled && self->_outline._isEnabled && AreOutlinesAvailable(self);

    // Bloom and sun shafts are added in HDR by the tonemap.
    float BloomStrength = (Environment->IsBloomEnabled && self->_bloom._isEnabled)
        ? (Environment->BloomStrength * WorldRendererCommon_ConfigBloomStrength(self) * BLOOM_BASE_INTENSITY) : 0.0f;
    frame->IsBloomActive = (BloomStrength > 0.0f) && IsBloomAvailable(self);
    frame->BloomStrength = frame->IsBloomActive ? BloomStrength : 0.0f;

    // Sun shafts also need the sun to project to a usable on-screen position (above the horizon, in front, not
    // too far off-screen). The strength fades with the sun's screen-margin/elevation weight so the shafts dim
    // gradually as the camera turns away, rather than switching off abruptly at the margin.
    float SunshaftStrength = (Environment->AreSunshaftsEnabled && self->_sunshaft._isEnabled)
        ? (Environment->SunshaftStrength * WorldRendererCommon_ConfigSunshaftStrength(self) * SUNSHAFT_BASE_INTENSITY)
        : 0.0f;
    float SunshaftFade = 0.0f;
    frame->AreSunshaftsActive = (SunshaftStrength > 0.0f) && AreSunshaftsAvailable(self)
        && WorldRendererPost_ComputeSunScreenUv(frame, &frame->SunScreenUv, &SunshaftFade);
    frame->SunshaftStrength = frame->AreSunshaftsActive ? (SunshaftStrength * SunshaftFade) : 0.0f;

    // The night sky (needs WorldRendererSky_PrepareFrame first): stars while some star is bright enough for the
    // twilight and the light pollution, the Milky Way while any of it shows.
    frame->AreStarsActive = AreStarsAvailable(self) && self->_stars._hasMesh
        && (frame->LimitingMagnitude > NIGHT_SKY_BRIGHTEST_MAGNITUDE);
    frame->IsMilkyWayActive = IsMilkyWayAvailable(self) && (frame->MilkyWayRadiance > 0.0f);
    frame->IsMoonActive = IsMoonAvailable(self) && WorldRendererSky_IsMoonVisible(frame);
}

/* A feature's state from what it needs (availability) and whether it ran. */
static WorldRendererFeatureState StateOf(bool isAvailable, bool isActive)
{
    if (!isAvailable)
    {
        return WorldRendererFeatureState_Unavailable;
    }
    return isActive ? WorldRendererFeatureState_Active : WorldRendererFeatureState_Inactive;
}

/* Records what this frame did, for WorldRenderer_GetStatus. */
static void UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererStatus* Status = &self->_status;
    Status->IsSceneHdr = self->_scene._isHdr;
    Status->IsSceneDepthSamplable = self->_scene._isDepthSamplable;

    bool HasPbr = (self->_pbr._shader != NULL);
    bool HasSky = (self->_sky._shader != NULL);
    bool HasTonemap = (self->_tonemap._shader != NULL);
    bool HasSprites = WorldRendererSprites_IsAvailable(self);
    Status->Features[WorldRendererFeature_PbrLighting] = StateOf(HasPbr, HasPbr);
    Status->Features[WorldRendererFeature_Sky] = StateOf(HasSky, HasSky);
    Status->Features[WorldRendererFeature_Tonemap] = StateOf(HasTonemap, HasTonemap);
    Status->Features[WorldRendererFeature_SpriteShading] = StateOf(HasSprites, HasSprites);
    Status->Features[WorldRendererFeature_SpriteLighting] = StateOf(HasSprites, self->_isSpriteLightingEnabled);
    Status->Features[WorldRendererFeature_SpriteShadows] = StateOf(AreSpriteShadowsAvailable(self),
        DoSpritesCastShadows(self, frame));
    Status->Features[WorldRendererFeature_SunShadows] = StateOf(IsCascadeShadowAvailable(self),
        frame->IsCascadeShadowActive && (frame->ShadowLight == WorldShadowLight_Sun));
    Status->Features[WorldRendererFeature_MoonShadows] = StateOf(IsCascadeShadowAvailable(self),
        frame->IsCascadeShadowActive && (frame->ShadowLight == WorldShadowLight_Moon));
    Status->Features[WorldRendererFeature_PointLightShadow] = StateOf(IsPointShadowAvailable(self),
        frame->IsPointShadowActive);
    Status->Features[WorldRendererFeature_AmbientOcclusion] = StateOf(IsAoAvailable(self), frame->IsAoActive);
    Status->Features[WorldRendererFeature_Outlines] = StateOf(AreOutlinesAvailable(self), frame->AreOutlinesActive);
    Status->Features[WorldRendererFeature_Bloom] = StateOf(IsBloomAvailable(self), frame->IsBloomActive);
    Status->Features[WorldRendererFeature_Sunshafts] = StateOf(AreSunshaftsAvailable(self), frame->AreSunshaftsActive);
    Status->Features[WorldRendererFeature_CrispOverlay] = StateOf(IsCrispAvailable(self), frame->IsCrispActive);
    Status->Features[WorldRendererFeature_Stars] = StateOf(AreStarsAvailable(self), frame->AreStarsActive);
    Status->Features[WorldRendererFeature_MilkyWay] = StateOf(IsMilkyWayAvailable(self), frame->IsMilkyWayActive);
    Status->Features[WorldRendererFeature_Moon] = StateOf(IsMoonAvailable(self), frame->IsMoonActive);
    Status->Features[WorldRendererFeature_Moonlight] = StateOf(IsMoonlightAvailable(self),
        frame->MoonLight.Intensity > 0.0f);
    Status->Features[WorldRendererFeature_NightVision] = StateOf(IsNightVisionAvailable(self),
        frame->IsNightVisionActive);
    Status->StarCount = self->_stars._starCount;

    size_t ModelCount = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    Status->ModelCount = 0;
    Status->ModelWithoutAssetCount = 0;
    for (size_t Index = 0; Index < ModelCount; Index++)
    {
        if (Models[Index].Asset != NULL)
        {
            Status->ModelCount++;
        }
        else
        {
            Status->ModelWithoutAssetCount++;
        }
    }
    size_t SpriteCount = 0;
    const WorldRenderSprite* Sprites = WorldRenderCache_GetSprites(&self->_cache, &SpriteCount);
    Status->SpriteCount = SpriteCount;
    Status->DecalCount = 0;
    Status->TransparentSpriteCount = 0;
    for (size_t Index = 0; Index < SpriteCount; Index++)
    {
        if (Sprites[Index].IsDecal)
        {
            Status->DecalCount++;
        }
        else if (Sprites[Index].AlphaMode != WorldSpriteAlphaMode_Cutout)
        {
            Status->TransparentSpriteCount++;
        }
    }
    WorldRenderCache_GetLights(&self->_cache, &Status->LightCount);
}

/* Appends "<label>name, name" (or "<label>none") for the features in @p state to the status line. */
static bool AppendFeatureList(GenericBuffer* line, const WorldRendererStatus* status, WorldRendererFeatureState state,
    const unsigned char* label)
{
    bool IsAppended = GenericBuffer_AppendString(line, label);
    bool IsFirst = true;
    for (int Feature = 0; Feature < (int)WorldRendererFeature_Count; Feature++)
    {
        if (status->Features[Feature] != state)
        {
            continue;
        }
        if (!IsFirst)
        {
            IsAppended = IsAppended && GenericBuffer_AppendString(line, (const unsigned char*)u8", ");
        }
        IsAppended = IsAppended
            && GenericBuffer_AppendString(line, WorldRenderer_GetFeatureName((WorldRendererFeature)Feature));
        IsFirst = false;
    }
    if (IsFirst)
    {
        IsAppended = IsAppended && GenericBuffer_AppendString(line, (const unsigned char*)u8"none");
    }
    return IsAppended;
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

    AssetUserID ShaderUser = ASSET_USER_ID_INVALID;
    Error UserResult = AssetManager_GetNewUserID(assetManager, &ShaderUser);
    if (UserResult.Code != ErrorCode_Success)
    {
        return UserResult;
    }
    AssetUserID ModelUser = ASSET_USER_ID_INVALID;
    UserResult = AssetManager_GetNewUserID(assetManager, &ModelUser);
    if (UserResult.Code != ErrorCode_Success)
    {
        Error RetireResult = AssetManager_RetireUser(assetManager, ShaderUser);
        Error_Deconstruct(&RetireResult);
        return UserResult;
    }

    WorldRenderer* Renderer = Memory_Allocate(sizeof(WorldRenderer));
    Memory_Zero(Renderer, sizeof(*Renderer));
    Renderer->_assetManager = assetManager;
    Renderer->_logger = logger;
    Renderer->_config = config;
    Renderer->_shaderUser = ShaderUser;
    Renderer->_modelUser = ModelUser;
    Renderer->_drawDebugGrid = true;
    Renderer->_lightGizmosEnabled = true;
    Renderer->_pixelationEnabled = true;
    Renderer->_postfxEnabled = true;
    Renderer->_isNightVisionEnabled = true;
    Renderer->_isSpriteLightingEnabled = true;
    Renderer->_nightVision = NightVision_GetCalibration();
    Renderer->_outline._isEnabled = true;
    Renderer->_bloom._isEnabled = true;
    Renderer->_sunshaft._isEnabled = true;
    Renderer->_crisp._isEnabled = true;
    WorldRenderCache_Construct(&Renderer->_cache);
    WorldRendererSky_Construct(Renderer);
    WorldRendererSprites_Construct(Renderer);
    GenericBuffer_AllocateVariable(&Renderer->_pbr._forwardLights, INITIAL_FORWARD_LIGHT_CAPACITY,
        sizeof(WorldCullingLight));
    GenericBuffer_AllocateVariable(&Renderer->_pbr._forwardRadiances, INITIAL_FORWARD_LIGHT_CAPACITY,
        sizeof(WorldRendererRadiance));
    for (int Feature = 0; Feature < (int)WorldRendererFeature_Count; Feature++)
    {
        Renderer->_status.Features[Feature] = WorldRendererFeatureState_Inactive;
    }

    *outRenderer = Renderer;
    return Error_CreateSuccess();
}

Error WorldRenderer_Deconstruct(WorldRenderer* self)
{
    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    WorldRendererScene_ReleaseTargets(self);
    WorldRendererShadows_Unload(self);
    WorldRendererStars_Unload(self);
    WorldRendererSprites_Deconstruct(self);
    WorldRendererSky_Deconstruct(self); // retires the sky textures' own asset users

    // The cache's asset pointers die with the model user, so the cache goes first.
    WorldRenderCache_Deconstruct(&self->_cache);
    Memory_Free(self->_pbr._forwardLights._data);
    Memory_Free(self->_pbr._forwardRadiances._data);

    Error Result = AssetManager_RetireUser(self->_assetManager, self->_modelUser);
    Error ShaderResult = AssetManager_RetireUser(self->_assetManager, self->_shaderUser);
    if (Result.Code == ErrorCode_Success)
    {
        Result = ShaderResult;
    }
    else
    {
        Error_Deconstruct(&ShaderResult);
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

    // Preload the shaders and the sky textures the world names, so the first frame does not stall on them (idempotent,
    // logged on failure).
    EnsureShaders(self);
    WorldRendererSky_SyncTextures(self, World_GetEnvironment(world));

    // Asset holds are not counted, so the models no object uses any more are released by resolving every model
    // the world uses under a fresh user and then retiring the old one: models both users hold stay loaded (no
    // reload), the rest unload. The cache forgets its entries first, so none keeps a pointer into a released model.
    AssetUserID NewUser = ASSET_USER_ID_INVALID;
    Error UserResult = AssetManager_GetNewUserID(self->_assetManager, &NewUser);
    if (UserResult.Code != ErrorCode_Success)
    {
        return UserResult;
    }
    AssetUserID OldUser = self->_modelUser;
    WorldRenderCache_Reset(&self->_cache);
    self->_modelUser = NewUser;

    Error SyncResult = WorldRenderCache_Sync(&self->_cache, world, self->_assetManager, NewUser, self->_logger);
    Error RetireResult = AssetManager_RetireUser(self->_assetManager, OldUser);
    if (SyncResult.Code != ErrorCode_Success)
    {
        Error_Deconstruct(&RetireResult);
        return SyncResult;
    }
    return RetireResult;
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

    // One walk of the world: resolves new and renamed models (failures logged once) and lists what to draw.
    Error SyncResult = WorldRenderCache_Sync(&self->_cache, world, self->_assetManager, self->_modelUser,
        self->_logger);
    if (SyncResult.Code != ErrorCode_Success)
    {
        return SyncResult;
    }
    // Recreates the targets after a resize or a pixelation change; only a missing scene target is fatal.
    Error TargetResult = WorldRendererScene_EnsureTargets(self, target.texture.width, target.texture.height);
    if (TargetResult.Code != ErrorCode_Success)
    {
        return TargetResult;
    }

    AdvanceRenderTime(self, deltaSeconds);
    WorldRenderFrame Frame;
    BuildFrame(self, world, camera, target, &Frame);
    // The star catalogue follows the world's seed and density, the sky textures its names (each rebuilt or reloaded
    // only when they change).
    WorldRendererStars_SyncCatalogue(self, Frame.Environment);
    WorldRendererSky_SyncTextures(self, Frame.Environment);
    WorldRendererSky_PrepareFrame(&Frame);
    DecidePasses(self, &Frame);
    WorldRendererLighting_PrepareFrame(self, &Frame);
    DecideNightVision(self, &Frame);
    // Every sprite's matrices for the camera and the shadow lights, and the transparent ones' draw order.
    WorldRendererSprites_PrepareFrame(self, &Frame);

    // Shadow maps first: the scene and crisp passes sample them.
    if (Frame.IsCascadeShadowActive)
    {
        WorldRendererShadows_RenderCascades(self, &Frame);
    }
    if (Frame.IsPointShadowActive)
    {
        WorldRendererShadows_RenderPointLight(self, &Frame);
    }

    // The G-buffer feeds BOTH the AO pre-pass (samplable depth + surface flag) and the outline pass; it is drawn
    // ONCE, before the scene, whenever either runs. AO then runs as a PRE-pass so the scene pass can sample it into
    // the ambient term. Outlines still run AFTER the scene (they read the scene colour).
    if (Frame.IsAoActive || Frame.AreOutlinesActive)
    {
        WorldRendererGBuffer_RenderNormals(self, &Frame);
    }
    if (Frame.IsAoActive)
    {
        WorldRendererGBuffer_RenderAo(self, &Frame);
    }

    // The 3D world into the scene target (pixel resolution when pixelating).
    WorldRendererScene_Render(self, &Frame);

    // Outlines (low-res, reading the scene colour + depth + G-buffer), then bloom and sun shafts from the
    // (outlined) HDR scene. Without outlines the scene goes to the tonemap as-is.
    RenderTexture2D CompositeSource = self->_scene._target;
    if (Frame.AreOutlinesActive)
    {
        WorldRendererPost_RenderOutlines(self, &Frame);
        CompositeSource = self->_outline._target;
    }
    if (Frame.IsBloomActive)
    {
        WorldRendererPost_RenderBloom(self, CompositeSource);
    }
    if (Frame.AreSunshaftsActive)
    {
        WorldRendererPost_RenderSunshafts(self, &Frame, CompositeSource);
    }

    // HDR eye adaptation eases the exposure toward the scene brightness, so the day-night cycle stays readable (a
    // dark night is lifted; a bright light pulls it back down). Then the tonemapped upscale into the frame target.
    Frame.Exposure = WorldRendererLighting_UpdateExposure(self, &Frame, (float)deltaSeconds);
    WorldRendererPost_Tonemap(self, &Frame, CompositeSource, target);

    // Last: the crisp overlay draws over the finished pixelated image (e.g. a readable 3D screen), depth-tested
    // against the scene so it is occluded correctly.
    if (Frame.IsCrispActive)
    {
        WorldRendererCrisp_Render(self, &Frame);
        WorldRendererCrisp_Composite(self, &Frame, target);
    }

    UpdateStatus(self, &Frame);
    if (!self->_isStatusReported)
    {
        self->_isStatusReported = true;
        WorldRenderer_LogStatus(self);
    }
    return Error_CreateSuccess();
}

void WorldRenderer_GetStatus(const WorldRenderer* self, WorldRendererStatus* outStatus)
{
    *outStatus = self->_status;
}

const unsigned char* WorldRenderer_GetFeatureName(WorldRendererFeature feature)
{
    switch (feature)
    {
        case WorldRendererFeature_PbrLighting:      return (const unsigned char*)u8"PBR lighting";
        case WorldRendererFeature_Sky:              return (const unsigned char*)u8"sky";
        case WorldRendererFeature_Tonemap:          return (const unsigned char*)u8"tonemap";
        case WorldRendererFeature_SpriteShading:    return (const unsigned char*)u8"sprite shading";
        case WorldRendererFeature_SunShadows:       return (const unsigned char*)u8"sun shadows";
        case WorldRendererFeature_PointLightShadow: return (const unsigned char*)u8"point-light shadow";
        case WorldRendererFeature_AmbientOcclusion: return (const unsigned char*)u8"ambient occlusion";
        case WorldRendererFeature_Outlines:         return (const unsigned char*)u8"outlines";
        case WorldRendererFeature_Bloom:            return (const unsigned char*)u8"bloom";
        case WorldRendererFeature_Sunshafts:        return (const unsigned char*)u8"sun shafts";
        case WorldRendererFeature_CrispOverlay:     return (const unsigned char*)u8"crisp overlay";
        case WorldRendererFeature_Stars:            return (const unsigned char*)u8"stars";
        case WorldRendererFeature_MilkyWay:         return (const unsigned char*)u8"Milky Way";
        case WorldRendererFeature_MoonShadows:      return (const unsigned char*)u8"moon shadows";
        case WorldRendererFeature_Moon:             return (const unsigned char*)u8"moon";
        case WorldRendererFeature_Moonlight:        return (const unsigned char*)u8"moonlight";
        case WorldRendererFeature_NightVision:      return (const unsigned char*)u8"night vision";
        case WorldRendererFeature_SpriteLighting:   return (const unsigned char*)u8"sprite lighting";
        case WorldRendererFeature_SpriteShadows:    return (const unsigned char*)u8"sprite shadows";
        case WorldRendererFeature_Count:            break;
    }
    return (const unsigned char*)u8"unknown";
}

void WorldRenderer_LogStatus(const WorldRenderer* self)
{
    if (self->_logger == NULL)
    {
        return;
    }

    const WorldRendererStatus* Status = &self->_status;
    GenericBuffer Lists;
    GenericBuffer_AllocateVariable(&Lists, STATUS_LINE_CAPACITY, sizeof(unsigned char));
    bool IsBuilt =
        AppendFeatureList(&Lists, Status, WorldRendererFeatureState_Active, (const unsigned char*)u8"active: ")
        && AppendFeatureList(&Lists, Status, WorldRendererFeatureState_Inactive, (const unsigned char*)u8"; inactive: ")
        && AppendFeatureList(&Lists, Status, WorldRendererFeatureState_Unavailable,
            (const unsigned char*)u8"; unavailable: ")
        && GenericBuffer_NullTerminate(&Lists);

    const char* Tier = !Status->IsSceneHdr ? "8-bit"
        : (Status->IsSceneDepthSamplable ? "HDR with samplable depth" : "HDR without samplable depth");
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: %s scene; %" PRIu64 u8" models (%" PRIu64 u8" without a model), %"
        PRIu64 u8" sprites (%" PRIu64 u8" decals, %" PRIu64 u8" blended or additive), %" PRIu64 u8" lights, %" PRIu64
        u8" stars; %s.",
        Tier, (uint64_t)Status->ModelCount, (uint64_t)Status->ModelWithoutAssetCount, (uint64_t)Status->SpriteCount,
        (uint64_t)Status->DecalCount, (uint64_t)Status->TransparentSpriteCount, (uint64_t)Status->LightCount,
        (uint64_t)Status->StarCount, IsBuilt ? (const char*)Lists._data : "feature list unavailable");
    Error_Deconstruct(&LogResult);
    Memory_Free(Lists._data);
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

void WorldRenderer_SetNightVisionEnabled(WorldRenderer* self, bool enabled)
{
    self->_isNightVisionEnabled = enabled;
}

bool WorldRenderer_IsNightVisionEnabled(const WorldRenderer* self)
{
    return self->_isNightVisionEnabled;
}

void WorldRenderer_SetSpriteLightingEnabled(WorldRenderer* self, bool enabled)
{
    self->_isSpriteLightingEnabled = enabled;
}

bool WorldRenderer_IsSpriteLightingEnabled(const WorldRenderer* self)
{
    return self->_isSpriteLightingEnabled;
}

void WorldRenderer_SetBloomEnabled(WorldRenderer* self, bool enabled)
{
    self->_bloom._isEnabled = enabled;
}

bool WorldRenderer_IsBloomEnabled(const WorldRenderer* self)
{
    return self->_bloom._isEnabled;
}

void WorldRenderer_SetSunshaftsEnabled(WorldRenderer* self, bool enabled)
{
    self->_sunshaft._isEnabled = enabled;
}

bool WorldRenderer_AreSunshaftsEnabled(const WorldRenderer* self)
{
    return self->_sunshaft._isEnabled;
}

void WorldRenderer_SetCrispOverlayEnabled(WorldRenderer* self, bool enabled)
{
    self->_crisp._isEnabled = enabled;
}

bool WorldRenderer_IsCrispOverlayEnabled(const WorldRenderer* self)
{
    return self->_crisp._isEnabled;
}

void WorldRenderer_SetDebugGridEnabled(WorldRenderer* self, bool enabled)
{
    self->_drawDebugGrid = enabled;
}

bool WorldRenderer_IsDebugGridEnabled(const WorldRenderer* self)
{
    return self->_drawDebugGrid;
}

void WorldRenderer_SetLightGizmosEnabled(WorldRenderer* self, bool enabled)
{
    self->_lightGizmosEnabled = enabled;
}

bool WorldRenderer_AreLightGizmosEnabled(const WorldRenderer* self)
{
    return self->_lightGizmosEnabled;
}
