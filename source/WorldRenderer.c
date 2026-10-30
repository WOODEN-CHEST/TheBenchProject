// The world renderer's public API: creation and teardown, world preparation, and RenderToTarget, which builds the
// per-frame context, decides which passes run, and runs them in order. The passes live in WorldRenderer*.c (see
// WorldRendererInternal.h); keep this file about orchestration and keep each pass's code in its own file.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "WorldLightCulling.h"
#include "NightSky.h"
#include "Logger.h"
#include "wr/WRMemory.h"
#include "wr/WRStopwatch.h"
#include "wr/WRString.h"


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
/** Initial capacity (in bytes) of the status line's LOD counts ("9 / 3 / 1"). */
#define STATUS_LOD_TEXT_CAPACITY 64
/** Nanoseconds in a millisecond and milliseconds in a second, for the render's CPU time and the frame time. */
#define NANOSECONDS_PER_MILLISECOND 1.0e6
#define MILLISECONDS_PER_SECOND 1000.0

_Static_assert(WORLD_RENDERER_STATUS_LOD_COUNT == WORLD_MODEL_MAX_LODS,
    "the status counts drawn models for every LOD a world model may have");


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

    // Reversed depth for the camera's passes before any target is made: their depth buffers follow it.
    self->_depth._isReversed = GLDepth_LoadApi(&self->_depth._api);
    if (!self->_depth._isReversed)
    {
        char Message[256];
        snprintf(Message, sizeof(Message), "WorldRenderer: reversed depth is unavailable (%s); the camera keeps raylib's "
            "projection and sees to %.0f m.", (self->_depth._api._missing != NULL) ? self->_depth._api._missing : "?",
            (double)SCENE_FAR_PLANE);
        WorldRendererCommon_WarnOnce(self, &self->_depth._isFailureReported, (const unsigned char*)Message);
    }

    // The atmosphere's tables first: the sky and every lit shader read them.
    WorldRendererAtmosphere_LoadShaders(self);
    WorldRendererLighting_LoadShader(self);
    WorldRendererGeometry_LoadShaders(self);
    WorldRendererPost_LoadShaders(self);
    WorldRendererExposure_Load(self);
    WorldRendererCrisp_LoadShader(self);
    WorldRendererSprites_LoadShaders(self);
    WorldRendererGBuffer_LoadShaders(self);
    WorldRendererShadows_LoadShaders(self);
    WorldRendererSky_LoadShader(self);
    WorldRendererStars_LoadShader(self);
    WorldRendererTerrain_LoadShader(self);
    // The sky's occupancy volume, its programs before the instanced twins.
    WorldRendererSkyVisibility_Load(self);
    // The instanced twins sit beside the lit, G-buffer, shadow and occupancy volume's programs loaded above.
    WorldRendererInstances_LoadShaders(self);
}

// What each optional feature needs to be able to run at all (its shaders and targets), independent of whether it
// is wanted this frame.
static bool AreShadowLevelsAvailable(const WorldRenderer* self)
{
    return WorldRendererShadows_AreLevelsAvailable(self) && (self->_pbr._rigid._shader != NULL);
}

static bool IsPointShadowAvailable(const WorldRenderer* self)
{
    return self->_pointShadow._hasCube && (self->_pointShadow._shader != NULL) && (self->_pbr._rigid._shader != NULL);
}

static bool IsAoAvailable(const WorldRenderer* self)
{
    return (self->_pbr._rigid._shader != NULL) && (self->_ao._shader != NULL) && (self->_ao._blurShader != NULL)
        && (self->_gbuffer._shader != NULL) && self->_gbuffer._hasTarget && self->_gbuffer._isDepthSamplable
        && self->_ao._hasRawTarget && self->_ao._hasTarget;
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
    return (self->_crisp._compositeShader != NULL) && (self->_pbr._rigid._shader != NULL) && self->_crisp._hasTarget
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
    return self->_pbr._rigid._shader != NULL;
}

static bool IsNightVisionAvailable(const WorldRenderer* self)
{
    return (self->_pbr._rigid._shader != NULL) || (self->_sky._shader != NULL); // the passes that apply it (sprites too)
}

static bool AreSpriteShadowsAvailable(const WorldRenderer* self)
{
    return WorldRendererSprites_IsAvailable(self)
        && ((AreShadowLevelsAvailable(self) && (self->_shadowLevels._spriteDepthShader != NULL))
            || (IsPointShadowAvailable(self) && (self->_pointShadow._spriteShader != NULL)));
}

/* Whether a sprite casts into a shadow pass that runs this frame with its sprite shader (the visibility's lists hold
 * the casters each pass draws). */
static bool DoSpritesCastShadows(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!WorldRendererSprites_IsAvailable(self))
    {
        return false;
    }
    size_t LevelCount = 0;
    for (uint32_t Level = 0U; frame->AreShadowLevelsActive && (Level < frame->ShadowLevelCount); Level++)
    {
        size_t Count = 0;
        WorldVisibility_GetSprites(&self->_visibility, &self->_cache, WorldVisibility_GetShadowLevelPass(Level), &Count);
        LevelCount += Count;
    }
    size_t CubeCount = 0;
    WorldVisibility_GetSprites(&self->_visibility, &self->_cache, WorldVisibilityPass_PointShadow, &CubeCount);
    bool IsLevelCasting = frame->AreShadowLevelsActive && (self->_shadowLevels._spriteDepthShader != NULL)
        && (LevelCount > 0);
    bool IsCubeCasting = frame->IsPointShadowActive && (self->_pointShadow._spriteShader != NULL) && (CubeCount > 0);
    return IsLevelCasting || IsCubeCasting;
}

/* The eye's night vision this frame: how strongly it applies (the renderer toggle x the world's strength), the field
 * luminance it compares with (the light the eye is in, NightVisionField, so it runs after
 * WorldRendererLighting_PrepareSkyLight,
 * which also chose the frame's calibration; a non-finite one counts as daylight, changing nothing), the rods' grey (the
 * world's blue shift), and whether it changes anything: only while the field is below the full-colour luminance (by day
 * the shaders skip it). */
static void DecideNightVision(const WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    float Strength = fminf(fmaxf(Environment->NightVisionStrength, 0.0f), 1.0f); // fmaxf drops a NaN
    frame->NightVisionStrength = self->_isNightVisionEnabled ? Strength : 0.0f;
    frame->NightVisionFieldLuminance = isfinite(frame->NightVisionField)
        ? fmaxf(frame->NightVisionField, 0.0f) : frame->NightVision.FullColourLuminance;
    NightVision_GetRodTint(Environment->NightVisionBlueShift, frame->NightVisionTint);
    frame->IsNightVisionActive = IsNightVisionAvailable(self) && (frame->NightVisionStrength > 0.0f)
        && (frame->NightVisionFieldLuminance < frame->NightVision.FullColourLuminance);
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

/* Fills the frame's sky from the environment's model: the sun, the moon, their light above the air (what reaches the
 * surfaces, and which of them casts the shadows, comes with the sky's light: WorldRendererLighting_PrepareSkyLight) and
 * the stars' rotation. */
static void BuildSky(WorldRenderFrame* frame)
{
    WorldSkyLights Lights;
    WorldEnvironment_GetSkyLights(frame->Environment, &Lights);
    frame->SunDirection = Lights.SunDirection;
    frame->Daylight = Lights.Daylight;
    frame->MoonDirection = Lights.MoonDirection;
    frame->MoonPhase = Lights.MoonPhase;
    frame->MoonIllumination = Lights.MoonIllumination;
    frame->SunIntensity = Lights.SunIntensity;
    frame->MoonIntensity = Lights.MoonIntensity;
    frame->StarsToWorld = WorldEnvironment_GetStarsToWorld(frame->Environment);
}

/* Fills the frame's world, camera and sky fields and the scene matrices in render space (WorldRenderSpace_BuildView):
 * the origin (the camera's position), the camera at (0, 0, 0) with its rotation-only view, and the projection the camera's
 * passes draw with (the camera's vertical FOV, the scene aspect, raylib's near plane; the reversed infinite projection
 * where the GPU has clip control, else BeginMode3D's with raylib's far plane), so every pass that reconstructs
 * positions from depth, or rays from pixels, matches the geometry exactly. */
static void BuildFrame(const WorldRenderer* self, World* world, const GameCamera* camera, RenderTexture2D target,
    WorldRenderFrame* outFrame)
{
    *outFrame = (WorldRenderFrame){ 0 };
    outFrame->World = world;
    outFrame->Environment = World_GetEnvironment(world);
    BuildSky(outFrame);
    outFrame->TimeSeconds = (float)self->_renderTimeSeconds;
    outFrame->WindowWidth = target.texture.width;
    outFrame->WindowHeight = target.texture.height;
    outFrame->SceneWidth = self->_scene._width;
    outFrame->SceneHeight = self->_scene._height;
    outFrame->SceneFilter = WorldRendererPixelFilter_All;
    outFrame->ShadowLightIndex = SIZE_MAX;

    double Aspect = (outFrame->SceneHeight > 0) ? ((double)outFrame->SceneWidth / (double)outFrame->SceneHeight) : 1.0;
    outFrame->IsDepthReversed = self->_depth._isReversed;
    WorldRenderView View = WorldRenderSpace_BuildView(camera, Aspect, SCENE_NEAR_PLANE, SCENE_FAR_PLANE,
        outFrame->IsDepthReversed);
    outFrame->Origin = View.Origin;
    outFrame->RenderCamera = View.Camera;
    outFrame->ZoomFactor = WorldVisibility_GetZoomFactor(camera->FovY);
    outFrame->View = View.View;
    outFrame->Projection = View.Projection;
    outFrame->InvProjection = View.InvProjection;
    outFrame->ViewProjection = View.ViewProjection;
    outFrame->InvViewProjection = View.InvViewProjection;
    // No far plane with reversed depth: the draw distances alone decide how far the camera sees.
    outFrame->CameraFrustum = outFrame->IsDepthReversed ? WorldVisibility_ExtractReversedFrustum(outFrame->ViewProjection)
        : WorldVisibility_ExtractFrustum(outFrame->ViewProjection);
}

/* Picks every model's LOD for this frame (the camera's distance to it, the camera's zoom, the player's lod_distance_scale)
 * and advances the LOD cross-fades by the frame's real seconds. Before the visibility, which counts the LODs drawn. In
 * render space: the camera at the origin. */
static void UpdateLods(WorldRenderer* self, const WorldRenderFrame* frame, double deltaSeconds)
{
    WorldRenderLodView View =
    {
        .CameraPosition = { 0.0f, 0.0f, 0.0f },
        .ZoomFactor = frame->ZoomFactor,
        .DistanceScale = WorldRendererCommon_ConfigLodDistanceScale(self),
        .DeltaSeconds = (float)deltaSeconds
    };
    WorldRenderCache_UpdateLods(&self->_cache, &View, self->_logger);
}

/* Decides which shadow passes run this frame (both are rendered before the scene pass). The shadow levels follow the
 * sky's shadow light (the sun by day, the moon on bright nights, none while both are low or handing over:
 * WorldRendererLighting_PrepareSkyLight chose it from their light at the camera), with the player's quality and the
 * world's reach (none at a reach of 0); the point-light shadow is independent of the sky (shadows at night), cast by
 * the nearest shadow-casting light. Both need to be available and wanted (the world's switch, world x config strength
 * above 0). */
static void DecideShadows(WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    bool AreShadowsWanted = Environment->AreShadowsEnabled
        && ((Environment->ShadowStrength * WorldRendererCommon_ConfigShadowStrength(self)) > 0.0f);

    frame->ShadowLevelSettings = WorldRendererShadows_GetSettings(self, Environment);
    frame->AreShadowLevelsActive = AreShadowLevelsAvailable(self) && AreShadowsWanted
        && (frame->ShadowLight != WorldShadowLight_None) && (frame->ShadowFade > 0.0f)
        && (frame->ShadowLevelSettings.Reach > 0.0f);
    if (IsPointShadowAvailable(self) && AreShadowsWanted)
    {
        frame->ShadowLightIndex = WorldRendererShadows_SelectPointLight(self);
        frame->IsPointShadowActive = (frame->ShadowLightIndex != SIZE_MAX);
    }
}

/* Builds the frame's visibility from the render cache: what the camera's passes draw (its frustum, the draw distances
 * under the quality setting and the camera's zoom, the opacities), which casters each shadow pass that runs draws
 * (each shadow level's box, ring and size cut within the shadow distances and the reach, the cube's reach) and what keeps the
 * sky out in the sky's occupancy volume (its map's box).
 * Needs the shadow decisions, WorldRendererShadows_PrepareFrame and WorldRendererSkyVisibility_PrepareFrame. In render
 * space: the camera at the origin. */
static Error BuildVisibility(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldVisibilityView View =
    {
        .CameraPosition = { 0.0f, 0.0f, 0.0f },
        .CameraFrustum = frame->CameraFrustum,
        .IsCullingEnabled = self->_isCullingEnabled,
        .ShadowLevelCount = frame->AreShadowLevelsActive ? frame->ShadowLevelCount : 0U,
        .ShadowReach = frame->ShadowLevelSettings.Reach,
        .ShadowDistanceScale = WorldRendererCommon_ConfigShadowDistanceScale(self),
        .IsPointShadowActive = frame->IsPointShadowActive,
        .PointShadowPosition = frame->PointShadowPosition,
        .PointShadowReach = frame->PointShadowFar,
        .IsSkyVisibilityActive = frame->IsSkyVisibilityActive,
        .SkyVisibilityBox = frame->SkyMapBox,
        .DrawDistanceScale = WorldRendererCommon_ConfigDrawDistanceScale(self),
        .ZoomFactor = frame->ZoomFactor
    };
    for (uint32_t Level = 0U; Level < View.ShadowLevelCount; Level++)
    {
        View.ShadowLevels[Level] = (WorldVisibilityShadowLevel)
        {
            .IsRendered = frame->ShadowLevels[Level].IsRendered,
            .Box = frame->ShadowLevels[Level].Box,
            .MinRadius = WorldShadowLevels_GetMinRadius(&frame->ShadowLevelSettings, Level),
            .InnerInset = WorldShadowLevels_GetInnerInset(&frame->ShadowLevelSettings, Level)
        };
    }
    return WorldVisibility_Build(&self->_visibility, &self->_cache, &View);
}

/* Decides which of the other optional passes run this frame and with what strength: each needs to be available (its
 * shaders and targets), wanted (renderer toggle, world flag, world x config strength above 0) and applicable (something
 * to draw, the sun on screen). Needs the frame's visibility. */
static void DecidePasses(WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;

    // The crisp overlay draws the OmitPixelation objects the camera sees at window resolution over the pixelated frame.
    // When it runs, the pixelated passes skip those objects; when it cannot, they fall back to the pixelated passes so
    // they never vanish.
    frame->IsCrispActive = self->_crisp._isEnabled && IsCrispAvailable(self)
        && WorldVisibility_HasCrispObjects(&self->_visibility);
    frame->SceneFilter = frame->IsCrispActive ? WorldRendererPixelFilter_PixelatedOnly : WorldRendererPixelFilter_All;

    // AO into the ambient term and the outlines both read the G-buffer; both are under the post-effects master
    // toggle.
    float AoStrength = Environment->IsAmbientOcclusionEnabled
        ? (Environment->AmbientOcclusionStrength * WorldRendererCommon_ConfigAoStrength(self)) : 0.0f;
    frame->IsAoActive = self->_postfxEnabled && (AoStrength > 0.0f) && IsAoAvailable(self);
    frame->AoStrength = frame->IsAoActive ? AoStrength : 0.0f;
    // The contact shadows close the shadow levels' seams from the same pre-pass; it runs for either.
    frame->AreContactShadowsActive = self->_areContactShadowsEnabled && frame->AreShadowLevelsActive
        && IsAoAvailable(self);
    // So does the sky's visibility, per pixel (the lit passes read it there), while the occupancy volume is drawn.
    frame->IsSkyVisibilityOnScreen = frame->IsSkyVisibilityActive && IsAoAvailable(self);
    frame->IsAmbientPrepassActive = frame->IsAoActive || frame->AreContactShadowsActive
        || frame->IsSkyVisibilityOnScreen;
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

    // The night sky (needs WorldRendererSky_PrepareNightSky first): stars while some star is bright enough for the sky
    // behind it, the Milky Way while any of it shows.
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

    bool HasPbr = (self->_pbr._rigid._shader != NULL);
    bool HasSky = (self->_sky._shader != NULL);
    bool HasTonemap = (self->_tonemap._shader != NULL);
    bool HasSprites = WorldRendererSprites_IsAvailable(self);
    Status->Features[WorldRendererFeature_PbrLighting] = StateOf(HasPbr, HasPbr);
    Status->Features[WorldRendererFeature_Sky] = StateOf(HasSky, HasSky);
    Status->Features[WorldRendererFeature_Tonemap] = StateOf(HasTonemap, HasTonemap);
    Status->Features[WorldRendererFeature_SpriteShading] = StateOf(HasSprites, HasSprites);
    bool HasTerrain = (self->_terrain._shader != NULL);
    Status->Features[WorldRendererFeature_TerrainShading] = StateOf(HasTerrain, HasTerrain);
    Status->Features[WorldRendererFeature_TerrainMaterials] = WorldRendererTerrain_GetMaterialsState(self);
    Status->Features[WorldRendererFeature_SpriteLighting] = StateOf(HasSprites, self->_isSpriteLightingEnabled);
    Status->Features[WorldRendererFeature_SpriteShadows] = StateOf(AreSpriteShadowsAvailable(self),
        DoSpritesCastShadows(self, frame));
    Status->Features[WorldRendererFeature_SunShadows] = StateOf(AreShadowLevelsAvailable(self),
        frame->AreShadowLevelsActive && (frame->ShadowLight == WorldShadowLight_Sun));
    Status->Features[WorldRendererFeature_MoonShadows] = StateOf(AreShadowLevelsAvailable(self),
        frame->AreShadowLevelsActive && (frame->ShadowLight == WorldShadowLight_Moon));
    Status->Features[WorldRendererFeature_PointLightShadow] = StateOf(IsPointShadowAvailable(self),
        frame->IsPointShadowActive);
    Status->Features[WorldRendererFeature_AmbientOcclusion] = StateOf(IsAoAvailable(self), frame->IsAoActive);
    Status->Features[WorldRendererFeature_ContactShadows] = StateOf(IsAoAvailable(self), frame->AreContactShadowsActive);
    Status->Features[WorldRendererFeature_Outlines] = StateOf(AreOutlinesAvailable(self), frame->AreOutlinesActive);
    Status->Features[WorldRendererFeature_Bloom] = StateOf(IsBloomAvailable(self), frame->IsBloomActive);
    Status->Features[WorldRendererFeature_Sunshafts] = StateOf(AreSunshaftsAvailable(self), frame->AreSunshaftsActive);
    Status->Features[WorldRendererFeature_CrispOverlay] = StateOf(IsCrispAvailable(self), frame->IsCrispActive);
    Status->Features[WorldRendererFeature_Stars] = StateOf(AreStarsAvailable(self), frame->AreStarsActive);
    Status->Features[WorldRendererFeature_MilkyWay] = StateOf(IsMilkyWayAvailable(self), frame->IsMilkyWayActive);
    Status->Features[WorldRendererFeature_Moon] = StateOf(IsMoonAvailable(self), frame->IsMoonActive);
    Status->Features[WorldRendererFeature_Moonlight] = StateOf(IsMoonlightAvailable(self), frame->IsMoonLit);
    Status->Features[WorldRendererFeature_NightVision] = StateOf(IsNightVisionAvailable(self),
        frame->IsNightVisionActive);
    Status->StarCount = self->_stars._starCount;

    size_t ModelCount = 0;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    Status->ModelCount = 0;
    Status->ModelWithoutAssetCount = 0;
    Status->PosedModelCount = 0;
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
        if (Models[Index].IsDrawn && (Models[Index].Pose != NULL))
        {
            Status->PosedModelCount++;
        }
    }
    // Skinning needs the skinned twin of the shader models draw with: the PBR one, or the unlit one without PBR.
    bool HasSkinning = HasPbr
        ? ((self->_pbr._skinned._shader != NULL) && (self->_pbr._skinned._palette._location >= 0))
        : ((self->_unlit._skinnedShader != NULL) && (self->_unlit._skinnedPalette._location >= 0));
    Status->Features[WorldRendererFeature_Skinning] = StateOf(HasSkinning, HasSkinning
        && (Status->PosedModelCount > 0U));
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

    WorldVisibilityCounts Counts = WorldVisibility_GetCounts(&self->_visibility);
    Status->IsCullingEnabled = self->_isCullingEnabled;
    Status->DrawnModelCount = Counts.DrawnModelCount;
    Status->DrawnSpriteCount = Counts.DrawnSpriteCount;
    Status->OutsideViewCount = Counts.OutsideViewCount;
    Status->FadedOutCount = Counts.FadedOutCount;
    Status->FadingCount = Counts.FadingCount;
    Memory_Copy(Counts.DrawnModelsByLod, Status->DrawnModelsByLod, sizeof(Status->DrawnModelsByLod));
    Status->LodFadingCount = Counts.LodFadingCount;
    Status->IsLodTintingEnabled = self->_isLodTintingEnabled;

    size_t TerrainCount = 0;
    const WorldRenderTerrain* Terrains = WorldRenderCache_GetTerrains(&self->_cache, &TerrainCount);
    Status->TerrainCount = 0;
    Status->TerrainWithoutAssetCount = 0;
    for (size_t Index = 0; Index < TerrainCount; Index++)
    {
        if (Terrains[Index].Asset != NULL)
        {
            Status->TerrainCount++;
        }
        else
        {
            Status->TerrainWithoutAssetCount++;
        }
    }
    WorldRenderCache_GetTerrainChunks(&self->_cache, &Status->TerrainChunkCount);
    Status->DrawnTerrainChunkCount = Counts.DrawnTerrainChunkCount;
    Status->DrawnTerrainTriangleCount = Counts.DrawnTerrainTriangleCount;
    Status->IsTerrainKeyEnabled = self->_terrain._isDebugEnabled;
    Status->TerrainView = self->_terrain._view;

    size_t InstanceEntryCount = 0;
    const WorldRenderInstances* InstanceEntries = WorldRenderCache_GetInstances(&self->_cache, &InstanceEntryCount);
    Status->InstanceComponentCount = 0;
    Status->InstanceComponentWithoutAssetCount = 0;
    for (size_t Index = 0; Index < InstanceEntryCount; Index++)
    {
        if (InstanceEntries[Index].Asset != NULL)
        {
            Status->InstanceComponentCount++;
        }
        else
        {
            Status->InstanceComponentWithoutAssetCount++;
        }
    }
    size_t InstanceCellCount = 0;
    const WorldRenderInstanceCell* InstanceCells = WorldRenderCache_GetInstanceCells(&self->_cache, &InstanceCellCount);
    Status->InstanceCellCount = InstanceCellCount;
    Status->InstanceCount = 0;
    for (size_t Index = 0; Index < InstanceCellCount; Index++)
    {
        Status->InstanceCount += InstanceCells[Index].RecordCount;
    }
    Status->DrawnInstanceCellCount = Counts.DrawnInstanceCellCount;
    Status->DrawnInstanceCount = Counts.DrawnInstanceCount;
    Status->InstanceCellsOutsideViewCount = Counts.InstanceCellsOutsideViewCount;
    Status->InstanceCellsFadedOutCount = Counts.InstanceCellsFadedOutCount;
    Status->IsInstancingEnabled = self->_instances._isEnabled;
    Status->Features[WorldRendererFeature_Instancing] = WorldRendererInstances_GetState(self);

    Status->Features[WorldRendererFeature_AtmosphereTables] = StateOf(WorldRendererAtmosphere_IsAvailable(self),
        frame->AreAtmosphereTablesActive);
    Status->AreAtmosphereTablesEnabled = self->_atmosphere._isEnabled;
    Status->AtmosphereTableBuildCount = self->_atmosphere._tables.BuildCount;
    Status->IrradianceTableBuildCount = self->_atmosphere._irradianceTable.BuildCount;
    Status->AtmosphereViewHeight = frame->AtmosphereViewHeight;
    Status->Features[WorldRendererFeature_Haze] = StateOf(WorldRendererAtmosphere_IsVolumeAvailable(self),
        frame->IsHazeActive);
    Status->HazeDistanceScale = frame->Haze.DistanceScale;
    Status->GroundHazeDensity = frame->Haze.GroundHazeDensity;
    Status->HazeView = self->_atmosphere._view;
    Status->HazeViewSlice = self->_atmosphere._viewSlice;

    // The light at the camera.
    const WorldSkyLightFrame* Light = &frame->SkyLight;
    Status->IsSkyLightPerHeight = frame->IsSkyLightPerHeight;
    Status->SkyLightTableBuildCount = self->_atmosphere._skyLight.Record.BuildCount;
    Status->IsSkyLightTableBuilding = self->_atmosphere._skyLight.IsBuilding;
    Memory_Copy(Light->Sun, Status->SunLight, sizeof(Status->SunLight));
    Memory_Copy(Light->Moon, Status->MoonLight, sizeof(Status->MoonLight));
    Memory_Copy(Light->Sky, Status->SkyIrradiance, sizeof(Status->SkyIrradiance));
    Memory_Copy(Light->Ground, Status->GroundIrradiance, sizeof(Status->GroundIrradiance));
    Memory_Copy(Light->Night, Status->NightLight, sizeof(Status->NightLight));
    Status->SceneLuminance = frame->BaseSceneLuminance;
    Status->Exposure = frame->Exposure;
    Status->Features[WorldRendererFeature_ExposureMetering] = StateOf(WorldRendererExposure_IsAvailable(self),
        frame->IsExposureMetered);
    Status->Features[WorldRendererFeature_ReversedDepth] = StateOf(self->_depth._isReversed, frame->IsDepthReversed);
    Status->AdaptedLuminance = frame->AdaptedLuminance;
    Status->AdaptedRealLuminance = frame->AdaptedRealLuminance;
    Status->IsExposureMetered = frame->IsExposureMetered;

    // Interior lighting.
    Status->IsInteriorLightingEnabled = self->_interior._isEnabled;
    WorldRenderCache_GetAmbientZones(&self->_cache, &Status->AmbientZoneCount);
    size_t OccludingModels = 0;
    size_t OccludingCells = 0;
    size_t OccludingChunks = 0;
    WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_SkyVisibility, &OccludingModels);
    WorldVisibility_GetInstanceCells(&self->_visibility, &self->_cache, WorldVisibilityPass_SkyVisibility,
        &OccludingCells);
    WorldVisibility_GetTerrainChunks(&self->_visibility, &self->_cache, WorldVisibilityPass_SkyVisibility,
        &OccludingChunks);
    Status->SkyOccluderCount = frame->IsSkyVisibilityActive ? (OccludingModels + OccludingCells + OccludingChunks) : 0U;
    Status->CameraOpenness = frame->CameraInterior.IsStarted ? frame->CameraInterior.Openness : 1.0f;
    Status->CameraZoneWeight = frame->CameraInterior.IsStarted ? frame->CameraInterior.Zones.Weight : 0.0f;
    Status->CameraSceneLuminance = frame->CameraSceneLuminance;
    Status->ExposureBias = frame->ExposureBias;
    Status->InteriorView = self->_interior._view;
    WorldRendererShadows_UpdateStatus(self, frame);
    Status->Features[WorldRendererFeature_SkyVisibility] = StateOf(WorldRendererSkyVisibility_IsAvailable(self),
        frame->IsSkyVisibilityActive);
    // The terrain's and the sprites' shaders take the zones too, so they run without the PBR shader as well.
    Status->Features[WorldRendererFeature_AmbientZones] = StateOf(true,
        frame->IsInteriorActive && (Status->AmbientZoneCount > 0U));
}

/* Appends the drawn models per LOD to the status line's LOD text, LOD 0 first, up to the coarsest LOD drawn: "9 / 3 / 1".
 * The text is NUL-terminated. */
static bool AppendLodCounts(GenericBuffer* text, const WorldRendererStatus* status)
{
    size_t LastLod = 0U;
    for (size_t Lod = 0U; Lod < WORLD_RENDERER_STATUS_LOD_COUNT; Lod++)
    {
        if (status->DrawnModelsByLod[Lod] > 0U)
        {
            LastLod = Lod;
        }
    }
    bool IsAppended = true;
    for (size_t Lod = 0U; (Lod <= LastLod) && IsAppended; Lod++)
    {
        Error Result = StringUTF8_Format(u8"%s%" PRIu64, text, (Lod == 0U) ? "" : " / ",
            (uint64_t)status->DrawnModelsByLod[Lod]);
        IsAppended = Result.Code == ErrorCode_Success;
        Error_Deconstruct(&Result);
    }
    return IsAppended && GenericBuffer_NullTerminate(text);
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
    Renderer->_areContactShadowsEnabled = true;
    Renderer->_isNightVisionEnabled = true;
    Renderer->_isSpriteLightingEnabled = true;
    Renderer->_nightVision = NightVision_GetCalibration();
    Renderer->_outline._isEnabled = true;
    Renderer->_bloom._isEnabled = true;
    Renderer->_sunshaft._isEnabled = true;
    Renderer->_crisp._isEnabled = true;
    Renderer->_isCullingEnabled = true;
    WorldRenderCache_Construct(&Renderer->_cache);
    WorldVisibility_Construct(&Renderer->_visibility);
    WorldRendererAtmosphere_Construct(Renderer);
    WorldRendererSky_Construct(Renderer);
    WorldRendererSprites_Construct(Renderer);
    WorldRendererTerrain_Construct(Renderer);
    WorldRendererInstances_Construct(Renderer);
    WorldRendererInterior_Construct(Renderer);
    GenericBuffer_AllocateVariable(&Renderer->_pbr._forwardLights, INITIAL_FORWARD_LIGHT_CAPACITY,
        sizeof(WorldCullingLight));
    GenericBuffer_AllocateVariable(&Renderer->_pbr._forwardRadiances, INITIAL_FORWARD_LIGHT_CAPACITY,
        sizeof(WorldRendererRadiance));
    GenericBuffer_AllocateVariable(&Renderer->_skyVisibility._modelLayers, 0U, sizeof(uint8_t));
    GenericBuffer_AllocateVariable(&Renderer->_skyVisibility._modelBoxes, 0U, sizeof(BoundingBox));
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
    WorldRendererExposure_Unload(self);
    WorldRendererAtmosphere_Unload(self);
    WorldRendererAtmosphere_Deconstruct(self);
    WorldRendererShadows_Unload(self);
    WorldRendererSkyVisibility_Unload(self);
    WorldRendererStars_Unload(self);
    WorldRendererSprites_Deconstruct(self);
    WorldRendererSky_Deconstruct(self); // retires the sky textures' own asset users

    // The cache's asset pointers die with the model user, so the cache (and the terrains' GPU meshes) go first.
    WorldRendererTerrain_Deconstruct(self);
    WorldRendererInstances_Deconstruct(self);
    WorldVisibility_Deconstruct(&self->_visibility);
    WorldRenderCache_Deconstruct(&self->_cache);
    Memory_Free(self->_pbr._forwardLights._data);
    Memory_Free(self->_skyVisibility._modelLayers._data);
    Memory_Free(self->_skyVisibility._modelBoxes._data);
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
    // logged on failure), and make the C mirror's tables hold the world's air (a whole build, about 70 ms, unless they
    // already hold it; without this the first frame builds them, and a new world's air would otherwise come in over
    // frames).
    EnsureShaders(self);
    WorldRendererSky_SyncTextures(self, World_GetEnvironment(world));
    WorldAtmosphereMedium Medium;
    WorldAtmosphere_GetMedium(World_GetEnvironment(world), &Medium);
    WorldSkyLight_PrepareBuilder(&self->_atmosphere._skyLight, &Medium);
    // The camera's interior starts afresh in a new world (its first frame sets it at once).
    WorldInteriorCamera_Construct(&self->_interior._camera);

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
    // The terrains' GPU meshes are keyed by their terrains, which the old user may release: another terrain could then
    // live at the same address. The copies' buffers go too: the new world's entries build their cells afresh.
    WorldRendererTerrain_ReleaseMeshes(self);
    WorldRendererInstances_ReleaseBuffers(self);
    self->_modelUser = NewUser;

    // Any origin will do: this sync resolves the world models, and every render syncs again for its camera.
    Error SyncResult = WorldRenderCache_Sync(&self->_cache, world, Vector3Double_Zero(), self->_assetManager, NewUser,
        self->_logger);
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

    Stopwatch RenderClock = Stopwatch_StartNew();
    EnsureShaders(self);

    // One walk of the world: resolves new and renamed models (failures logged once) and lists what to draw, in render
    // space: every position relative to the camera's (the frame's origin).
    Error SyncResult = WorldRenderCache_Sync(&self->_cache, world, camera->Position, self->_assetManager,
        self->_modelUser, self->_logger);
    if (SyncResult.Code != ErrorCode_Success)
    {
        return SyncResult;
    }
    // The skinned shaders' palettes hold no pose yet this frame (an animator's memory may be another's since the last).
    WorldRendererGeometry_ResetPalettes(self);
    // The terrains' chunks on the GPU: new and changed ones uploaded, those of terrains no longer placed unloaded.
    WorldRendererTerrain_PrepareFrame(self);
    // The copies' records on the GPU: new builds of cells uploaded, the buffers no component holds unloaded.
    WorldRendererInstances_PrepareFrame(self);
    // Recreates the targets after a resize or a pixelation change; only a missing scene target is fatal.
    Error TargetResult = WorldRendererScene_EnsureTargets(self, target.texture.width, target.texture.height);
    if (TargetResult.Code != ErrorCode_Success)
    {
        return TargetResult;
    }

    AdvanceRenderTime(self, deltaSeconds);
    WorldRenderFrame Frame;
    BuildFrame(self, world, camera, target, &Frame);
    // Whether interior lighting runs, and the camera's interior: how open its surroundings are and the zones at it (the
    // sky's light below takes the light the eye is in from it).
    WorldRendererInterior_PrepareCamera(self, &Frame, (float)deltaSeconds);
    // The star catalogue follows the world's seed and density, the sky textures its names (each rebuilt or reloaded
    // only when they change).
    WorldRendererStars_SyncCatalogue(self, Frame.Environment);
    WorldRendererSky_SyncTextures(self, Frame.Environment);
    WorldRendererSky_PrepareFrame(&Frame);
    // The air, the haze and the height the sky is seen from, and whether the atmosphere's tables draw the sky and the
    // volume hazes it this frame. Then the sky's light: what reaches the surfaces at the camera through the air, the
    // sky's irradiance there, the light the air is lit by, the scene luminance the eye adapts from, the night vision's
    // calibration and the shadows' light.
    WorldRendererAtmosphere_PrepareFrame(self, &Frame);
    WorldRendererLighting_PrepareSkyLight(self, &Frame);
    // The stars and the Milky Way by the sky's brightness behind them (the twilight from the sky's light).
    WorldRendererSky_PrepareNightSky(&Frame);
    // Every model's LOD and its cross-fade, then which shadow passes run and their light matrices and the sky's visibility
    // map's box, then what every pass draws: the camera's frustum, each shadow level's box, the cube's reach and the map's box
    // cull, the draw distances and opacities fade.
    UpdateLods(self, &Frame, deltaSeconds);
    DecideShadows(self, &Frame);
    WorldRendererShadows_PrepareFrame(self, &Frame);
    WorldRendererSkyVisibility_PrepareFrame(self, &Frame);
    Error VisibilityResult = BuildVisibility(self, &Frame);
    if (VisibilityResult.Code != ErrorCode_Success)
    {
        // The shadow levels were told they render this frame: they will not, so none of their maps is what it says.
        WorldShadowLevels_Invalidate(&self->_shadowLevels._levels);
        return VisibilityResult;
    }
    DecidePasses(self, &Frame);
    WorldRendererLighting_PrepareFrame(self, &Frame);
    DecideNightVision(self, &Frame);
    // Every sprite's matrices for the camera and the shadow lights, and the transparent ones' draw order.
    WorldRendererSprites_PrepareFrame(self, &Frame);

    // The atmosphere's tables and the haze's volume first: the sky, the stars and every lit surface read them.
    // Then the shadow maps and the sky's occupancy volume: the pre-pass, the scene and crisp passes sample them.
    WorldRendererAtmosphere_Render(self, &Frame);
    if (Frame.AreShadowLevelsActive)
    {
        WorldRendererShadows_RenderLevels(self, &Frame);
    }
    if (Frame.IsPointShadowActive)
    {
        WorldRendererShadows_RenderPointLight(self, &Frame);
    }
    // The sky's occupancy volume: the ambient pre-pass and the lit passes read it for every surface's share of the sky's
    // light.
    if (Frame.IsSkyVisibilityActive)
    {
        WorldRendererSkyVisibility_Render(self, &Frame);
    }

    // The G-buffer feeds BOTH the ambient pre-pass (samplable depth + surface flag) and the outline pass; it is drawn
    // ONCE, before the scene, whenever either runs. The pre-pass (the AO, the contact shadows) then runs before the
    // scene pass, which samples it. Outlines still run AFTER the scene (they read the scene colour).
    if (Frame.IsAmbientPrepassActive || Frame.AreOutlinesActive)
    {
        WorldRendererGBuffer_RenderNormals(self, &Frame);
    }
    if (Frame.IsAmbientPrepassActive)
    {
        WorldRendererGBuffer_RenderAo(self, &Frame);
    }

    // The 3D world into the scene target (pixel resolution when pixelating), and over it, when asked, the haze's debug
    // view (one slice of the volume).
    WorldRendererScene_Render(self, &Frame);
    WorldRendererAtmosphere_DrawView(self, &Frame);

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
        WorldRendererPost_RenderBloom(self, &Frame, CompositeSource);
    }
    if (Frame.AreSunshaftsActive)
    {
        WorldRendererPost_RenderSunshafts(self, &Frame, CompositeSource);
    }

    // The eye adapts to the picture: it eases toward the brightness the meter measured a frame or two ago, and
    // meters this one. Then the tonemapped upscale into the frame target.
    Frame.Exposure = WorldRendererExposure_Update(self, &Frame, CompositeSource, (float)deltaSeconds);
    WorldRendererPost_Tonemap(self, &Frame, CompositeSource, target);

    // Last: the crisp overlay draws over the finished pixelated image (e.g. a readable 3D screen), depth-tested
    // against the scene so it is occluded correctly.
    if (Frame.IsCrispActive)
    {
        WorldRendererCrisp_Render(self, &Frame);
        WorldRendererCrisp_Composite(self, &Frame, target);
    }

    UpdateStatus(self, &Frame);
    self->_status.RenderMilliseconds = (double)Stopwatch_ElapsedNanoseconds(&RenderClock) / NANOSECONDS_PER_MILLISECOND;
    self->_status.FrameMilliseconds = (isfinite(deltaSeconds) && (deltaSeconds > 0.0))
        ? (deltaSeconds * MILLISECONDS_PER_SECOND) : 0.0;
    Stopwatch_Deconstruct(&RenderClock);
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
        case WorldRendererFeature_Skinning:         return (const unsigned char*)u8"skinning";
        case WorldRendererFeature_TerrainShading:   return (const unsigned char*)u8"terrain shading";
        case WorldRendererFeature_TerrainMaterials: return (const unsigned char*)u8"terrain materials";
        case WorldRendererFeature_Instancing:       return (const unsigned char*)u8"instancing";
        case WorldRendererFeature_AtmosphereTables: return (const unsigned char*)u8"atmosphere tables";
        case WorldRendererFeature_Haze:             return (const unsigned char*)u8"haze";
        case WorldRendererFeature_ExposureMetering: return (const unsigned char*)u8"exposure metering";
        case WorldRendererFeature_ReversedDepth:    return (const unsigned char*)u8"reversed depth";
        case WorldRendererFeature_SkyVisibility:    return (const unsigned char*)u8"sky visibility";
        case WorldRendererFeature_AmbientZones:     return (const unsigned char*)u8"ambient zones";
        case WorldRendererFeature_ContactShadows:   return (const unsigned char*)u8"contact shadows";
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
    GenericBuffer Lods;
    GenericBuffer_AllocateVariable(&Lods, STATUS_LOD_TEXT_CAPACITY, sizeof(unsigned char));
    bool AreLodsBuilt = AppendLodCounts(&Lods, Status);

    const char* Tier = !Status->IsSceneHdr ? "8-bit"
        : (Status->IsSceneDepthSamplable ? "HDR with samplable depth" : "HDR without samplable depth");
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: %s scene; %" PRIu64 u8" models (%" PRIu64 u8" without a world model, %"
        PRIu64 u8" posed), %" PRIu64 u8" sprites (%" PRIu64 u8" decals, %" PRIu64 u8" blended or additive), %" PRIu64
        u8" lights, %" PRIu64 u8" stars; drawn %" PRIu64 u8" models and %" PRIu64 u8" sprites (%" PRIu64 u8" fading; "
        u8"models by LOD %s, %" PRIu64 u8" cross-fading%s), culled %" PRIu64 u8" outside the view%s and %" PRIu64
        u8" faded out (draw distance, opacity); terrain: %" PRIu64 u8" placed (%" PRIu64 u8" without a terrain), "
        u8"%" PRIu64 u8" of %" PRIu64 u8" chunks drawn, %" PRIu64 u8" triangles%s%s%s; %s.",
        Tier, (uint64_t)Status->ModelCount, (uint64_t)Status->ModelWithoutAssetCount, (uint64_t)Status->PosedModelCount,
        (uint64_t)Status->SpriteCount, (uint64_t)Status->DecalCount, (uint64_t)Status->TransparentSpriteCount,
        (uint64_t)Status->LightCount,
        (uint64_t)Status->StarCount, (uint64_t)Status->DrawnModelCount, (uint64_t)Status->DrawnSpriteCount,
        (uint64_t)Status->FadingCount, AreLodsBuilt ? (const char*)Lods._data : "?",
        (uint64_t)Status->LodFadingCount, Status->IsLodTintingEnabled ? ", LOD tinting on" : "",
        (uint64_t)Status->OutsideViewCount, Status->IsCullingEnabled ? "" : " (frustum culling off)",
        (uint64_t)Status->FadedOutCount, (uint64_t)Status->TerrainCount, (uint64_t)Status->TerrainWithoutAssetCount,
        (uint64_t)Status->DrawnTerrainChunkCount, (uint64_t)Status->TerrainChunkCount,
        (uint64_t)Status->DrawnTerrainTriangleCount, Status->IsTerrainKeyEnabled ? ", chunk key on" : "",
        (Status->TerrainView != WorldRendererTerrainView_Shaded) ? ", showing " : "",
        (Status->TerrainView != WorldRendererTerrainView_Shaded)
            ? (const char*)WorldRenderer_GetTerrainViewName(Status->TerrainView) : "",
        IsBuilt ? (const char*)Lists._data : "feature list unavailable");
    Error_Deconstruct(&LogResult);
    Memory_Free(Lods._data);
    Memory_Free(Lists._data);
    // Each terrain's materials and texture arrays, the copies of the model instances components, the atmosphere's
    // tables with the render's CPU time, the light at the camera, the shadow levels and interior lighting.
    WorldRendererTerrain_LogMaterials(self);
    WorldRendererInstances_LogStatus(self);
    WorldRendererAtmosphere_LogStatus(self);
    WorldRendererLighting_LogStatus(self);
    WorldRendererShadows_LogStatus(self);
    WorldRendererInterior_LogStatus(self);
}

void WorldRenderer_SetPixelationEnabled(WorldRenderer* self, bool enabled)
{
    self->_pixelationEnabled = enabled;
}

bool WorldRenderer_IsPixelationEnabled(const WorldRenderer* self)
{
    return self->_pixelationEnabled;
}

void WorldRenderer_SetAtmosphereTablesEnabled(WorldRenderer* self, bool enabled)
{
    self->_atmosphere._isEnabled = enabled;
}

bool WorldRenderer_AreAtmosphereTablesEnabled(const WorldRenderer* self)
{
    return self->_atmosphere._isEnabled;
}

void WorldRenderer_SetHazeView(WorldRenderer* self, WorldRendererHazeView view, int slice)
{
    if ((uint32_t)view >= (uint32_t)WorldRendererHazeView_Count)
    {
        return;
    }
    self->_atmosphere._view = view;
    self->_atmosphere._viewSlice = (slice < 0) ? 0
        : ((slice > WORLD_RENDERER_HAZE_SLICE_COUNT - 1) ? (WORLD_RENDERER_HAZE_SLICE_COUNT - 1) : slice);
}

WorldRendererHazeView WorldRenderer_GetHazeView(const WorldRenderer* self, int* outSlice)
{
    if (outSlice != NULL)
    {
        *outSlice = self->_atmosphere._viewSlice;
    }
    return self->_atmosphere._view;
}

double WorldRenderer_GetHazeSliceDistance(int slice)
{
    return ((slice >= 0) && (slice <= WORLD_ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE))
        ? WorldAtmosphere_GetVolumeSliceDistance(slice) : 0.0;
}

const unsigned char* WorldRenderer_GetHazeViewName(WorldRendererHazeView view)
{
    switch (view)
    {
        case WorldRendererHazeView_Off:           return (const unsigned char*)u8"off";
        case WorldRendererHazeView_InScatter:     return (const unsigned char*)u8"in-scatter";
        case WorldRendererHazeView_Transmittance: return (const unsigned char*)u8"transmittance";
        case WorldRendererHazeView_Count:         break;
    }
    return (const unsigned char*)u8"unknown";
}

void WorldRenderer_SetInteriorLightingEnabled(WorldRenderer* self, bool enabled)
{
    self->_interior._isEnabled = enabled;
}

bool WorldRenderer_IsInteriorLightingEnabled(const WorldRenderer* self)
{
    return self->_interior._isEnabled;
}

void WorldRenderer_SetCameraOpenness(WorldRenderer* self, float openness)
{
    self->_interior._cameraOpenness = isnan(openness) ? 1.0f : fminf(fmaxf(openness, 0.0f), 1.0f);
}

void WorldRenderer_SetInteriorView(WorldRenderer* self, WorldRendererInteriorView view)
{
    if ((unsigned int)view < (unsigned int)WorldRendererInteriorView_Count)
    {
        self->_interior._view = view;
    }
}

WorldRendererInteriorView WorldRenderer_GetInteriorView(const WorldRenderer* self)
{
    return self->_interior._view;
}

const unsigned char* WorldRenderer_GetInteriorViewName(WorldRendererInteriorView view)
{
    switch (view)
    {
        case WorldRendererInteriorView_Off:   return (const unsigned char*)u8"off";
        case WorldRendererInteriorView_Zones: return (const unsigned char*)u8"zones";
        case WorldRendererInteriorView_Sky:   return (const unsigned char*)u8"sky share";
        case WorldRendererInteriorView_Count: break;
    }
    return (const unsigned char*)u8"unknown";
}

void WorldRenderer_SetPostEffectsEnabled(WorldRenderer* self, bool enabled)
{
    self->_postfxEnabled = enabled;
}

bool WorldRenderer_ArePostEffectsEnabled(const WorldRenderer* self)
{
    return self->_postfxEnabled;
}

void WorldRenderer_SetContactShadowsEnabled(WorldRenderer* self, bool enabled)
{
    self->_areContactShadowsEnabled = enabled;
}

bool WorldRenderer_AreContactShadowsEnabled(const WorldRenderer* self)
{
    return self->_areContactShadowsEnabled;
}

void WorldRenderer_SetShadowLevelViewEnabled(WorldRenderer* self, bool enabled)
{
    self->_shadowLevels._isLevelViewEnabled = enabled;
}

bool WorldRenderer_IsShadowLevelViewEnabled(const WorldRenderer* self)
{
    return self->_shadowLevels._isLevelViewEnabled;
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

void WorldRenderer_SetCullingEnabled(WorldRenderer* self, bool enabled)
{
    self->_isCullingEnabled = enabled;
}

bool WorldRenderer_IsCullingEnabled(const WorldRenderer* self)
{
    return self->_isCullingEnabled;
}

void WorldRenderer_SetLodTintingEnabled(WorldRenderer* self, bool enabled)
{
    self->_isLodTintingEnabled = enabled;
}

bool WorldRenderer_IsLodTintingEnabled(const WorldRenderer* self)
{
    return self->_isLodTintingEnabled;
}

void WorldRenderer_SetTerrainKeyEnabled(WorldRenderer* self, bool enabled)
{
    self->_terrain._isDebugEnabled = enabled;
}

bool WorldRenderer_IsTerrainKeyEnabled(const WorldRenderer* self)
{
    return self->_terrain._isDebugEnabled;
}

void WorldRenderer_SetInstancingEnabled(WorldRenderer* self, bool enabled)
{
    self->_instances._isEnabled = enabled;
}

bool WorldRenderer_IsInstancingEnabled(const WorldRenderer* self)
{
    return self->_instances._isEnabled;
}

void WorldRenderer_SetTerrainView(WorldRenderer* self, WorldRendererTerrainView view)
{
    if ((uint32_t)view < (uint32_t)WorldRendererTerrainView_Count)
    {
        self->_terrain._view = view;
    }
}

WorldRendererTerrainView WorldRenderer_GetTerrainView(const WorldRenderer* self)
{
    return self->_terrain._view;
}

const unsigned char* WorldRenderer_GetTerrainViewName(WorldRendererTerrainView view)
{
    switch (view)
    {
        case WorldRendererTerrainView_Shaded:           return (const unsigned char*)u8"shaded";
        case WorldRendererTerrainView_PaintedWeights:   return (const unsigned char*)u8"painted weights";
        case WorldRendererTerrainView_RuleWeights:      return (const unsigned char*)u8"rule weights";
        case WorldRendererTerrainView_DominantMaterial: return (const unsigned char*)u8"dominant material";
        case WorldRendererTerrainView_Count:            break;
    }
    return (const unsigned char*)u8"unknown";
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

void WorldRenderer_SetFirstPersonObject(WorldRenderer* self, uint64_t objectId)
{
    WorldRenderCache_SetFirstPersonObject(&self->_cache, objectId);
}

uint64_t WorldRenderer_GetFirstPersonObject(const WorldRenderer* self)
{
    return self->_cache._firstPersonObjectId;
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

void WorldRenderer_SetCollisionDebug(WorldRenderer* self, const WorldCollision* collision)
{
    self->_collisionDebug = collision;
}

const WorldCollision* WorldRenderer_GetCollisionDebug(const WorldRenderer* self)
{
    return self->_collisionDebug;
}
