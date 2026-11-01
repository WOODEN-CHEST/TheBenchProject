// The clouds (shaders/common/clouds.glsl; their pure maths in WorldClouds.h, their noise in CloudNoise.h): the noise
// made at load, each frame the patterns' drift and the layers, the light that lights them, their shadow map around the
// camera, and their march over the sky's pixels into the cloud target, which the sky pass composites over the sky and the
// stars read; the lit passes read the shadow map. Drawn after the atmosphere's tables, the haze's volume and the
// G-buffer, before the scene.

#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "CloudNoise.h"
#include "Logger.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"
#include "wr/WRStopwatch.h"


// Macros.
/** Asset names of the march and the shadow map's builder (full-screen fragment passes). */
#define MARCH_SHADER_ASSET_NAME ((const unsigned char*)u8"world_clouds")
#define SHADOW_SHADER_ASSET_NAME ((const unsigned char*)u8"world_clouds_shadow")
#define RESOLVE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_clouds_resolve")
/** The share of each frame's march in the accumulated result, per quality (low, high): about the last 1 / share frames
 *  are averaged. */
#define LOW_QUALITY_CURRENT_SHARE 0.08f
#define HIGH_QUALITY_CURRENT_SHARE 0.05f
/** A camera that moved farther than this between two frames (a teleport) starts the accumulation again, metres. */
#define HISTORY_MAX_SHIFT 500.0
/** The shadow map's side, texels (WORLD_CLOUDS_SHADOW_EXTENT over it: about 47 m a texel). */
#define SHADOW_MAP_SIZE 512
/** The shadow map's steps through the low layer. */
#define SHADOW_STEPS 24
/** The steps per quality (low, high): the low layer's most samples (coarse and fine), its light steps, the mid layer's
 *  view steps. */
#define LOW_QUALITY_VIEW_STEPS 56
#define LOW_QUALITY_LIGHT_STEPS 4
#define LOW_QUALITY_MID_STEPS 8
#define HIGH_QUALITY_VIEW_STEPS 96
#define HIGH_QUALITY_LIGHT_STEPS 5
#define HIGH_QUALITY_MID_STEPS 16
/** The farthest a march reaches along its ray, metres: beyond, the air's haze hides the clouds. */
#define MAX_MARCH_DISTANCE 60000.0f
/** The town's light on the clouds' undersides: the irradiance per unit of light pollution's radiance in the air at the
 *  camera's foot (a town's lit ground and air below the clouds: an overcast night over it glows). */
#define TOWN_IRRADIANCE_PER_RADIANCE 3.0f
/** A flash's light in the clouds near its strike, per unit of its light on a level surface. */
#define FLASH_CLOUD_RADIANCE_PER_IRRADIANCE 2.0f
/** The frame index's wrap for the steps' jitter. */
#define FRAME_INDEX_WRAP 4096U
/** Milliseconds per nanosecond. */
#define MILLISECONDS_PER_NANOSECOND 1.0e-6


// Static functions.
/* Points a sampler uniform at a fixed texture unit, once at load (raylib's SetShaderValue ignores location -1). */
static void AssignSamplerUnit(Shader shader, int location, WorldRendererTextureUnit unit)
{
    int UnitIndex = (int)unit;
    SetShaderValue(shader, location, &UnitIndex, SHADER_UNIFORM_INT);
}

/* Binds a 2D texture on a fixed unit (a 2D batch draw binds only units 0 to 4), leaving unit 0 active. */
static void BindTexture2D(WorldRendererTextureUnit unit, unsigned int id)
{
    rlActiveTextureSlot((int)unit);
    rlEnableTexture(id);
    rlActiveTextureSlot(0);
}

/* A shader's common/clouds locations, its samplers pointed at their units. */
static void PrepareCloudLocations(Shader shader, WorldRendererCloudLocations* outLocations)
{
    outLocations->_locShapeNoise = GetShaderLocation(shader, "cloudShapeNoise");
    outLocations->_locDetailNoise = GetShaderLocation(shader, "cloudDetailNoise");
    outLocations->_locWeatherMap = GetShaderLocation(shader, "cloudWeatherMap");
    outLocations->_locLowLayer = GetShaderLocation(shader, "cloudLowLayer");
    outLocations->_locLowExtinction = GetShaderLocation(shader, "cloudLowExtinction");
    outLocations->_locDetailStrength = GetShaderLocation(shader, "cloudDetailStrength");
    outLocations->_locMidLayer = GetShaderLocation(shader, "cloudMidLayer");
    outLocations->_locMidExtinction = GetShaderLocation(shader, "cloudMidExtinction");
    outLocations->_locHighLayer = GetShaderLocation(shader, "cloudHighLayer");
    outLocations->_locShapeAnchor = GetShaderLocation(shader, "cloudShapeAnchor");
    outLocations->_locDetailAnchor = GetShaderLocation(shader, "cloudDetailAnchor");
    outLocations->_locCoverageAnchor = GetShaderLocation(shader, "cloudCoverageAnchor");
    outLocations->_locMidAnchor = GetShaderLocation(shader, "cloudMidAnchor");
    outLocations->_locHighAnchor = GetShaderLocation(shader, "cloudHighAnchor");
    outLocations->_locTiles = GetShaderLocation(shader, "cloudTiles");
    outLocations->_locMapTiles = GetShaderLocation(shader, "cloudMapTiles");
    AssignSamplerUnit(shader, outLocations->_locShapeNoise, WorldRendererTextureUnit_CloudShape);
    AssignSamplerUnit(shader, outLocations->_locDetailNoise, WorldRendererTextureUnit_CloudDetail);
    AssignSamplerUnit(shader, outLocations->_locWeatherMap, WorldRendererTextureUnit_CloudWeather);
}

/* Uploads the frame's layers, look and anchors to a shader's common/clouds uniforms. */
static void UploadClouds(Shader shader, const WorldRendererCloudLocations* locations, const WorldCloudsFrame* clouds)
{
    float Low[4] = { clouds->Low.Base, clouds->Low.Top, clouds->Low.Coverage, clouds->Low.Type };
    SetShaderValue(shader, locations->_locLowLayer, Low, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locations->_locLowExtinction, &clouds->Low.Extinction, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locDetailStrength, &clouds->DetailStrength, SHADER_UNIFORM_FLOAT);
    float Mid[4] = { clouds->Mid.Base, clouds->Mid.Top, clouds->Mid.Coverage, clouds->Mid.Type };
    SetShaderValue(shader, locations->_locMidLayer, Mid, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locations->_locMidExtinction, &clouds->Mid.Extinction, SHADER_UNIFORM_FLOAT);
    float High[4] = { clouds->HighHeight, clouds->HighCoverage, clouds->HighType, clouds->HighDepth };
    SetShaderValue(shader, locations->_locHighLayer, High, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locations->_locShapeAnchor, clouds->ShapeAnchor, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locDetailAnchor, clouds->DetailAnchor, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locCoverageAnchor, clouds->CoverageAnchor, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, locations->_locMidAnchor, clouds->MidAnchor, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locHighAnchor, clouds->HighAnchor, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, locations->_locTiles, clouds->Tiles, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locations->_locMapTiles, clouds->MapTiles, SHADER_UNIFORM_VEC2);
}

/* Binds the noise on its units (the 3D textures through GLTexture3D, the weather map manually). */
static void BindNoise(const WorldRendererCloudState* state)
{
    GLTexture3D_Bind(&state->_api, (int32_t)WorldRendererTextureUnit_CloudShape, state->_shapeNoise);
    GLTexture3D_Bind(&state->_api, (int32_t)WorldRendererTextureUnit_CloudDetail, state->_detailNoise);
    BindTexture2D(WorldRendererTextureUnit_CloudWeather, state->_weatherMap.id);
}

/* Makes the noise's three textures from CloudNoise.h. false (and nothing left behind) when the GPU refuses one; the
 * error's message is logged once. */
static bool MakeNoise(WorldRenderer* self)
{
    WorldRendererCloudState* State = &self->_clouds;
    Stopwatch Clock = Stopwatch_StartNew();
    size_t ShapeCount = (size_t)CLOUD_NOISE_SHAPE_SIZE * CLOUD_NOISE_SHAPE_SIZE * CLOUD_NOISE_SHAPE_SIZE;
    size_t DetailCount = (size_t)CLOUD_NOISE_DETAIL_SIZE * CLOUD_NOISE_DETAIL_SIZE * CLOUD_NOISE_DETAIL_SIZE;
    size_t WeatherCount = (size_t)CLOUD_NOISE_WEATHER_SIZE * CLOUD_NOISE_WEATHER_SIZE * 4U;
    uint8_t* Texels = Memory_Allocate(ShapeCount + DetailCount + WeatherCount);
    uint8_t* Shape = Texels;
    uint8_t* Detail = Texels + ShapeCount;
    uint8_t* Weather = Detail + DetailCount;
    CloudNoise_FillShape(Shape);
    CloudNoise_FillDetail(Detail);
    CloudNoise_FillWeather(Weather);
    double Milliseconds = (double)Stopwatch_ElapsedNanoseconds(&Clock) * MILLISECONDS_PER_NANOSECOND;
    Stopwatch_Deconstruct(&Clock);

    Error Result = GLTexture3D_Create(&State->_api, GLTexture3DFormat_R8, CLOUD_NOISE_SHAPE_SIZE, CLOUD_NOISE_SHAPE_SIZE,
        CLOUD_NOISE_SHAPE_SIZE, Shape, &State->_shapeNoise);
    if (Result.Code == ErrorCode_Success)
    {
        Result = GLTexture3D_Create(&State->_api, GLTexture3DFormat_R8, CLOUD_NOISE_DETAIL_SIZE, CLOUD_NOISE_DETAIL_SIZE,
            CLOUD_NOISE_DETAIL_SIZE, Detail, &State->_detailNoise);
    }
    if (Result.Code == ErrorCode_Success)
    {
        Image WeatherImage =
        {
            .data = Weather, .width = CLOUD_NOISE_WEATHER_SIZE, .height = CLOUD_NOISE_WEATHER_SIZE, .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
        };
        State->_weatherMap = LoadTextureFromImage(WeatherImage);
        if (State->_weatherMap.id != 0U)
        {
            GenTextureMipmaps(&State->_weatherMap);
            SetTextureFilter(State->_weatherMap, TEXTURE_FILTER_TRILINEAR);
            SetTextureWrap(State->_weatherMap, TEXTURE_WRAP_REPEAT);
        }
        else
        {
            Result = Error_Construct2(ErrorCode_InvalidState, "the GPU refused the clouds' weather map");
        }
    }
    Memory_Free(Texels);
    if (Result.Code != ErrorCode_Success)
    {
        char Message[512];
        snprintf(Message, sizeof(Message), "WorldRenderer: the clouds' noise could not be made (%s); no clouds are drawn.",
            (Result.Message != NULL) ? (const char*)Result.Message : "?");
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)Message);
        Error_Deconstruct(&Result);
        GLTexture3D_Delete(&State->_api, State->_shapeNoise);
        GLTexture3D_Delete(&State->_api, State->_detailNoise);
        State->_shapeNoise = 0U;
        State->_detailNoise = 0U;
        return false;
    }
    if (self->_logger != NULL)
    {
        Error LogResult = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"WorldRenderer: the clouds' noise took "
            u8"%.1f ms to make.", Milliseconds);
        Error_Deconstruct(&LogResult);
    }
    return true;
}

/* The march's own locations and samplers. */
static void PrepareMarch(WorldRendererCloudState* state)
{
    Shader March = GameShader_GetRaylibShader(state->_marchShader);
    WorldRendererAtmosphere_PrepareShader(March, &state->_marchAtmosphere);
    WorldRendererAtmosphere_PrepareHazeShader(March, true, &state->_marchHaze);
    PrepareCloudLocations(March, &state->_marchClouds);
    AssignSamplerUnit(March, state->_marchAtmosphere._locIrradianceTable, WorldRendererTextureUnit_CloudIrradiance);
    // Declared through common/atmosphere and unread by the march: kept off unit 0 all the same.
    AssignSamplerUnit(March, state->_marchAtmosphere._locMultiscatterTable, WorldRendererTextureUnit_CloudIrradiance);
    state->_marchLocResolution = GetShaderLocation(March, "resolution");
    state->_marchLocInvViewProj = GetShaderLocation(March, "invViewProj");
    state->_marchLocSkyMask = GetShaderLocation(March, "skyMask");
    state->_marchLocHasSkyMask = GetShaderLocation(March, "hasSkyMask");
    state->_marchLocSkyMaskResolution = GetShaderLocation(March, "skyMaskResolution");
    state->_marchLocSunDirection = GetShaderLocation(March, "sunDirection");
    state->_marchLocMoonDirection = GetShaderLocation(March, "moonDirection");
    state->_marchLocLightDirection = GetShaderLocation(March, "cloudLightDirection");
    state->_marchLocLightRadiance = GetShaderLocation(March, "cloudLightRadiance");
    state->_marchLocSunRadiance = GetShaderLocation(March, "cloudSunRadiance");
    state->_marchLocMoonRadiance = GetShaderLocation(March, "cloudMoonRadiance");
    state->_marchLocGroundIrradiance = GetShaderLocation(March, "cloudGroundIrradiance");
    state->_marchLocTownIrradiance = GetShaderLocation(March, "cloudTownIrradiance");
    state->_marchLocTint = GetShaderLocation(March, "cloudTint");
    state->_marchLocForward = GetShaderLocation(March, "cloudForward");
    state->_marchLocFlashRadiance = GetShaderLocation(March, "cloudFlashRadiance");
    state->_marchLocFlashPosition = GetShaderLocation(March, "cloudFlashPosition");
    state->_marchLocMaxDistance = GetShaderLocation(March, "cloudMaxDistance");
    state->_marchLocLowSteps = GetShaderLocation(March, "cloudLowSteps");
    state->_marchLocLightSteps = GetShaderLocation(March, "cloudLightSteps");
    state->_marchLocMidSteps = GetShaderLocation(March, "cloudMidSteps");
    state->_marchLocFrame = GetShaderLocation(March, "cloudFrame");
    AssignSamplerUnit(March, state->_marchLocSkyMask, WorldRendererTextureUnit_CloudSkyMask);
}

/* The shadow map builder's locations and samplers. */
static void PrepareShadow(WorldRendererCloudState* state)
{
    Shader Builder = GameShader_GetRaylibShader(state->_shadowShader);
    WorldRendererAtmosphere_PrepareShader(Builder, &state->_shadowAtmosphere);
    PrepareCloudLocations(Builder, &state->_shadowClouds);
    AssignSamplerUnit(Builder, state->_shadowAtmosphere._locIrradianceTable, WorldRendererTextureUnit_CloudIrradiance);
    AssignSamplerUnit(Builder, state->_shadowAtmosphere._locMultiscatterTable, WorldRendererTextureUnit_CloudIrradiance);
    state->_shadowLocResolution = GetShaderLocation(Builder, "resolution");
    state->_shadowLocCenter = GetShaderLocation(Builder, "cloudShadowCenter");
    state->_shadowLocExtent = GetShaderLocation(Builder, "cloudShadowExtent");
    state->_shadowLocPlane = GetShaderLocation(Builder, "cloudShadowPlane");
    state->_shadowLocLightDirection = GetShaderLocation(Builder, "cloudLightDirection");
    state->_shadowLocSteps = GetShaderLocation(Builder, "cloudShadowSteps");
}

/* The accumulation's locations. */
static void PrepareResolve(WorldRendererCloudState* state)
{
    Shader Resolve = GameShader_GetRaylibShader(state->_resolveShader);
    WorldRendererAtmosphere_PrepareShader(Resolve, &state->_resolveAtmosphere);
    PrepareCloudLocations(Resolve, &state->_resolveClouds);
    AssignSamplerUnit(Resolve, state->_resolveAtmosphere._locIrradianceTable, WorldRendererTextureUnit_CloudIrradiance);
    AssignSamplerUnit(Resolve, state->_resolveAtmosphere._locMultiscatterTable,
        WorldRendererTextureUnit_CloudIrradiance);
    state->_resolveLocCurrent = GetShaderLocation(Resolve, "currentClouds");
    state->_resolveLocHistory = GetShaderLocation(Resolve, "historyClouds");
    state->_resolveLocResolution = GetShaderLocation(Resolve, "resolution");
    state->_resolveLocInvViewProj = GetShaderLocation(Resolve, "invViewProj");
    state->_resolveLocPreviousViewProjection = GetShaderLocation(Resolve, "previousViewProjection");
    state->_resolveLocOriginShift = GetShaderLocation(Resolve, "originShift");
    state->_resolveLocHasHistory = GetShaderLocation(Resolve, "hasHistory");
    state->_resolveLocCurrentShare = GetShaderLocation(Resolve, "currentShare");
}

/* The cloud target's size for the scene's and the quality: the scene's at high, half at low, at most
 * WORLD_RENDERER_CLOUD_MAX_WIDTH wide (the scene's aspect kept). */
static void GetTargetSize(int32_t quality, int sceneWidth, int sceneHeight, int* outWidth, int* outHeight)
{
    int Width = (sceneWidth < 1) ? 1 : sceneWidth;
    int Height = (sceneHeight < 1) ? 1 : sceneHeight;
    if (Width > WORLD_RENDERER_CLOUD_MAX_WIDTH)
    {
        Height = (int)lroundf((float)Height * (float)WORLD_RENDERER_CLOUD_MAX_WIDTH / (float)Width);
        Width = WORLD_RENDERER_CLOUD_MAX_WIDTH;
    }
    if (quality == CONFIG_CLOUD_QUALITY_LOW)
    {
        Width = (Width + 1) / 2;
        Height = (Height + 1) / 2;
    }
    *outWidth = (Width < 1) ? 1 : Width;
    *outHeight = (Height < 1) ? 1 : Height;
}

/* A colour's luminance (Rec. 709). */
static float Luminance(const float color[3])
{
    return 0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2];
}

/* Draws the shadow map: the light's share through every layer for the plane's points around the camera. */
static void RenderShadowMap(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererCloudState* State = &self->_clouds;
    Shader Builder = GameShader_GetRaylibShader(State->_shadowShader);
    WorldRendererAtmosphere_UploadFrame(Builder, &State->_shadowAtmosphere, frame);
    UploadClouds(Builder, &State->_shadowClouds, &frame->Clouds);
    float Resolution[2] = { (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE };
    SetShaderValue(Builder, State->_shadowLocResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(Builder, State->_shadowLocCenter, frame->CloudShadow.Center, SHADER_UNIFORM_VEC2);
    SetShaderValue(Builder, State->_shadowLocExtent, &frame->CloudShadow.Extent, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Builder, State->_shadowLocPlane, &frame->CloudShadow.Plane, SHADER_UNIFORM_FLOAT);
    float Light[3] = { frame->CloudLightDirection.x, frame->CloudLightDirection.y, frame->CloudLightDirection.z };
    SetShaderValue(Builder, State->_shadowLocLightDirection, Light, SHADER_UNIFORM_VEC3);
    int Steps = SHADOW_STEPS;
    SetShaderValue(Builder, State->_shadowLocSteps, &Steps, SHADER_UNIFORM_INT);

    BeginTextureMode(State->_shadowMap);
    rlDisableColorBlend(); // every texel as computed
    BeginShaderMode(Builder);
    BindNoise(State);
    DrawRectangle(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, WHITE);
    EndShaderMode();
    rlEnableColorBlend();
    EndTextureMode();
}

/* Marches the clouds into the cloud target. */
static void RenderMarch(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererCloudState* State = &self->_clouds;
    Shader March = GameShader_GetRaylibShader(State->_marchShader);
    WorldRendererAtmosphere_UploadFrame(March, &State->_marchAtmosphere, frame);
    WorldRendererAtmosphere_UploadHaze(March, &State->_marchHaze, frame);
    UploadClouds(March, &State->_marchClouds, &frame->Clouds);
    float Resolution[2] = { (float)State->_width, (float)State->_height };
    SetShaderValue(March, State->_marchLocResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(March, State->_marchLocInvViewProj, frame->InvViewProjection);
    // The G-buffer's flags, while it was drawn this frame: texels whose scene pixels all hold geometry are not marched.
    bool HasSkyMask = (frame->IsAmbientPrepassActive || frame->AreOutlinesActive) && self->_gbuffer._hasTarget;
    int HasSkyMaskValue = HasSkyMask ? 1 : 0;
    SetShaderValue(March, State->_marchLocHasSkyMask, &HasSkyMaskValue, SHADER_UNIFORM_INT);
    float MaskResolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
    SetShaderValue(March, State->_marchLocSkyMaskResolution, MaskResolution, SHADER_UNIFORM_VEC2);
    float Sun[3] = { frame->SunDirection.x, frame->SunDirection.y, frame->SunDirection.z };
    float Moon[3] = { frame->MoonDirection.x, frame->MoonDirection.y, frame->MoonDirection.z };
    SetShaderValue(March, State->_marchLocSunDirection, Sun, SHADER_UNIFORM_VEC3);
    SetShaderValue(March, State->_marchLocMoonDirection, Moon, SHADER_UNIFORM_VEC3);
    float Light[3] = { frame->CloudLightDirection.x, frame->CloudLightDirection.y, frame->CloudLightDirection.z };
    SetShaderValue(March, State->_marchLocLightDirection, Light, SHADER_UNIFORM_VEC3);
    SetShaderValue(March, State->_marchLocLightRadiance, frame->CloudLightRadiance, SHADER_UNIFORM_VEC3);
    // The sky's light on the clouds: the irradiance table per unit of the lights above the air, in full.
    float SunRadiance[3];
    float MoonRadiance[3];
    float TownIrradiance[3];
    float FlashRadiance[3];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        SunRadiance[Channel] = frame->AirSunIntensity[Channel] * (float)WORLD_ATMOSPHERE_SUN_LIGHT;
        MoonRadiance[Channel] = frame->AirMoonIntensity[Channel] * (float)WORLD_ATMOSPHERE_SUN_LIGHT;
        TownIrradiance[Channel] = frame->LightPollutionRadiance[Channel] * TOWN_IRRADIANCE_PER_RADIANCE
            * frame->Clouds.LightPollution;
        FlashRadiance[Channel] = frame->Weather.FlashIrradiance[Channel] * FLASH_CLOUD_RADIANCE_PER_IRRADIANCE;
    }
    SetShaderValue(March, State->_marchLocSunRadiance, SunRadiance, SHADER_UNIFORM_VEC3);
    SetShaderValue(March, State->_marchLocMoonRadiance, MoonRadiance, SHADER_UNIFORM_VEC3);
    SetShaderValue(March, State->_marchLocGroundIrradiance, frame->SkyLight.Ground, SHADER_UNIFORM_VEC3);
    SetShaderValue(March, State->_marchLocTownIrradiance, TownIrradiance, SHADER_UNIFORM_VEC3);
    SetShaderValue(March, State->_marchLocTint, frame->Clouds.Tint, SHADER_UNIFORM_VEC3);
    SetShaderValue(March, State->_marchLocForward, &frame->Clouds.Forward, SHADER_UNIFORM_FLOAT);
    SetShaderValue(March, State->_marchLocFlashRadiance, FlashRadiance, SHADER_UNIFORM_VEC3);
    // The strike's place in the clouds: over its level direction and distance, in the low layer's lower part.
    Vector3 Flash = frame->Weather.FlashDirection;
    float FlashDistance = isfinite(frame->Environment->LightningDistance) ? frame->Environment->LightningDistance : 0.0f;
    float FlashLevel = sqrtf(Flash.x * Flash.x + Flash.z * Flash.z);
    float FlashPosition[3] =
    {
        (FlashLevel > 0.0f) ? (Flash.x / FlashLevel * FlashDistance) : 0.0f,
        frame->Clouds.Low.Base + 0.3f * (frame->Clouds.Low.Top - frame->Clouds.Low.Base) - frame->AtmosphereViewHeight,
        (FlashLevel > 0.0f) ? (Flash.z / FlashLevel * FlashDistance) : 0.0f
    };
    SetShaderValue(March, State->_marchLocFlashPosition, FlashPosition, SHADER_UNIFORM_VEC3);
    float MaxDistance = MAX_MARCH_DISTANCE;
    SetShaderValue(March, State->_marchLocMaxDistance, &MaxDistance, SHADER_UNIFORM_FLOAT);
    bool IsHigh = State->_quality == CONFIG_CLOUD_QUALITY_HIGH;
    int ViewSteps = IsHigh ? HIGH_QUALITY_VIEW_STEPS : LOW_QUALITY_VIEW_STEPS;
    int LightSteps = IsHigh ? HIGH_QUALITY_LIGHT_STEPS : LOW_QUALITY_LIGHT_STEPS;
    int MidSteps = IsHigh ? HIGH_QUALITY_MID_STEPS : LOW_QUALITY_MID_STEPS;
    SetShaderValue(March, State->_marchLocLowSteps, &ViewSteps, SHADER_UNIFORM_INT);
    SetShaderValue(March, State->_marchLocLightSteps, &LightSteps, SHADER_UNIFORM_INT);
    SetShaderValue(March, State->_marchLocMidSteps, &MidSteps, SHADER_UNIFORM_INT);
    float FrameValue = (float)(State->_frameIndex % FRAME_INDEX_WRAP);
    SetShaderValue(March, State->_marchLocFrame, &FrameValue, SHADER_UNIFORM_FLOAT);

    BeginTextureMode(State->_target);
    rlDisableColorBlend(); // every texel as computed (-1 in alpha where geometry covers it)
    BeginShaderMode(March);
    BindNoise(State);
    BindTexture2D(WorldRendererTextureUnit_AtmosphereTransmittance, self->_atmosphere._transmittance.texture.id);
    BindTexture2D(WorldRendererTextureUnit_HazeVolume, self->_atmosphere._volume.texture.id);
    BindTexture2D(WorldRendererTextureUnit_CloudIrradiance, self->_atmosphere._irradiance.texture.id);
    if (HasSkyMask)
    {
        BindTexture2D(WorldRendererTextureUnit_CloudSkyMask, self->_gbuffer._target.texture.id);
    }
    DrawRectangle(0, 0, State->_width, State->_height, WHITE);
    EndShaderMode();
    rlEnableColorBlend();
    EndTextureMode();
}


/* Blends this frame's march into the last result, reprojected, into the other history target, which then holds the
 * newest result. A jump of the camera, or no last result, starts again from the march. */
static void RenderResolve(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererCloudState* State = &self->_clouds;
    Shader Resolve = GameShader_GetRaylibShader(State->_resolveShader);
    WorldRendererAtmosphere_UploadFrame(Resolve, &State->_resolveAtmosphere, frame);
    UploadClouds(Resolve, &State->_resolveClouds, &frame->Clouds);
    double ShiftX = frame->Origin.X - State->_previousOrigin.X;
    double ShiftY = frame->Origin.Y - State->_previousOrigin.Y;
    double ShiftZ = frame->Origin.Z - State->_previousOrigin.Z;
    bool HasHistory = State->_hasPreviousFrame
        && ((ShiftX * ShiftX + ShiftY * ShiftY + ShiftZ * ShiftZ) < (HISTORY_MAX_SHIFT * HISTORY_MAX_SHIFT));
    int HasHistoryValue = HasHistory ? 1 : 0;
    SetShaderValue(Resolve, State->_resolveLocHasHistory, &HasHistoryValue, SHADER_UNIFORM_INT);
    float Shift[3] = { (float)ShiftX, (float)ShiftY, (float)ShiftZ };
    SetShaderValue(Resolve, State->_resolveLocOriginShift, Shift, SHADER_UNIFORM_VEC3);
    SetShaderValueMatrix(Resolve, State->_resolveLocPreviousViewProjection, State->_previousViewProjection);
    SetShaderValueMatrix(Resolve, State->_resolveLocInvViewProj, frame->InvViewProjection);
    float Resolution[2] = { (float)State->_width, (float)State->_height };
    SetShaderValue(Resolve, State->_resolveLocResolution, Resolution, SHADER_UNIFORM_VEC2);
    float Share = (State->_quality == CONFIG_CLOUD_QUALITY_HIGH) ? HIGH_QUALITY_CURRENT_SHARE
        : LOW_QUALITY_CURRENT_SHARE;
    SetShaderValue(Resolve, State->_resolveLocCurrentShare, &Share, SHADER_UNIFORM_FLOAT);

    int Last = State->_newest;
    int Next = 1 - Last;
    BeginTextureMode(State->_history[Next]);
    rlDisableColorBlend();
    BeginShaderMode(Resolve);
    SetShaderValueTexture(Resolve, State->_resolveLocCurrent, State->_target.texture);
    SetShaderValueTexture(Resolve, State->_resolveLocHistory, State->_history[Last].texture);
    DrawRectangle(0, 0, State->_width, State->_height, WHITE);
    EndShaderMode();
    rlEnableColorBlend();
    EndTextureMode();
    State->_newest = Next;
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererClouds_Load(WorldRenderer* self)
{
    WorldRendererCloudState* State = &self->_clouds;
    State->_marchShader = WorldRendererCommon_LoadShader(self, MARCH_SHADER_ASSET_NAME);
    State->_shadowShader = WorldRendererCommon_LoadShader(self, SHADOW_SHADER_ASSET_NAME);
    State->_resolveShader = WorldRendererCommon_LoadShader(self, RESOLVE_SHADER_ASSET_NAME);
    if (State->_marchShader == NULL)
    {
        return;
    }
    if (!GLTexture3D_LoadApi(&State->_api))
    {
        char Message[256];
        snprintf(Message, sizeof(Message), "WorldRenderer: 3D textures are unavailable (%s); no clouds are drawn.",
            (State->_api._missing != NULL) ? State->_api._missing : "?");
        WorldRendererCommon_WarnOnce(self, &State->_isFailureReported, (const unsigned char*)Message);
        return;
    }
    State->_hasNoise = MakeNoise(self);
    if (!State->_hasNoise)
    {
        return;
    }
    PrepareMarch(State);
    if (State->_resolveShader != NULL)
    {
        PrepareResolve(State);
    }
    if (State->_shadowShader != NULL)
    {
        PrepareShadow(State);
        State->_shadowMap = WorldRendererCommon_CreateColorTarget(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE,
            PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
        State->_hasShadowMap = State->_shadowMap.id != 0U;
        if (State->_hasShadowMap)
        {
            SetTextureFilter(State->_shadowMap.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(State->_shadowMap.texture, TEXTURE_WRAP_CLAMP);
        }
        else
        {
            WorldRendererCommon_WarnOnce(self, &State->_isShadowMapFailureReported, (const unsigned char*)u8"WorldRenderer: "
                u8"the clouds' shadow map could not be made; the clouds cast no shadows.");
        }
    }
}

void WorldRendererClouds_Unload(WorldRenderer* self)
{
    WorldRendererCloudState* State = &self->_clouds;
    if (State->_hasNoise)
    {
        GLTexture3D_Delete(&State->_api, State->_shapeNoise);
        GLTexture3D_Delete(&State->_api, State->_detailNoise);
        UnloadTexture(State->_weatherMap);
        State->_shapeNoise = 0U;
        State->_detailNoise = 0U;
        State->_weatherMap = (Texture2D){ 0 };
        State->_hasNoise = false;
    }
    WorldRendererCommon_UnloadTarget(&State->_shadowMap, &State->_hasShadowMap);
    WorldRendererClouds_ReleaseTargets(self);
}

void WorldRendererClouds_EnsureTarget(WorldRenderer* self, int sceneWidth, int sceneHeight)
{
    WorldRendererCloudState* State = &self->_clouds;
    if ((State->_marchShader == NULL) || !State->_hasNoise || (State->_quality == CONFIG_CLOUD_QUALITY_OFF))
    {
        WorldRendererClouds_ReleaseTargets(self);
        return;
    }
    int Width = 0;
    int Height = 0;
    GetTargetSize(State->_quality, sceneWidth, sceneHeight, &Width, &Height);
    // Made at this size already, or refused by the GPU at it (tried again when the size or the quality changes).
    if (State->_isTargetTried && (State->_width == Width) && (State->_height == Height))
    {
        return;
    }
    WorldRendererClouds_ReleaseTargets(self);
    State->_isTargetTried = true;
    State->_width = Width;
    State->_height = Height;
    State->_target = WorldRendererCommon_CreateColorTarget(Width, Height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    State->_hasTarget = State->_target.id != 0U;
    if (!State->_hasTarget)
    {
        WorldRendererCommon_WarnOnce(self, &State->_isTargetFailureReported, (const unsigned char*)u8"WorldRenderer: the "
            u8"cloud target could not be made; no clouds are drawn.");
        return;
    }
    // Read texel by texel (the readers weigh the valid ones themselves); clamped at its edges.
    SetTextureFilter(State->_target.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(State->_target.texture, TEXTURE_WRAP_CLAMP);
    // The accumulation's two targets, the same size (without them each frame's march is shown as it is).
    if (State->_resolveShader != NULL)
    {
        State->_history[0] = WorldRendererCommon_CreateColorTarget(Width, Height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
        State->_history[1] = WorldRendererCommon_CreateColorTarget(Width, Height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
        State->_hasHistory = (State->_history[0].id != 0U) && (State->_history[1].id != 0U);
        for (int Index = 0; Index < 2; Index++)
        {
            if (State->_history[Index].id != 0U)
            {
                SetTextureFilter(State->_history[Index].texture, TEXTURE_FILTER_POINT);
                SetTextureWrap(State->_history[Index].texture, TEXTURE_WRAP_CLAMP);
            }
        }
        if (!State->_hasHistory)
        {
            bool HadFirst = State->_history[0].id != 0U;
            bool HadSecond = State->_history[1].id != 0U;
            WorldRendererCommon_UnloadTarget(&State->_history[0], &HadFirst);
            WorldRendererCommon_UnloadTarget(&State->_history[1], &HadSecond);
            WorldRendererCommon_WarnOnce(self, &State->_isHistoryFailureReported, (const unsigned char*)u8"WorldRenderer: "
                u8"the clouds' accumulation targets could not be made; each frame's clouds are shown as marched.");
        }
    }
    State->_hasPreviousFrame = false;
}

void WorldRendererClouds_ReleaseTargets(WorldRenderer* self)
{
    WorldRendererCloudState* State = &self->_clouds;
    WorldRendererCommon_UnloadTarget(&State->_target, &State->_hasTarget);
    bool HasHistory = State->_hasHistory;
    WorldRendererCommon_UnloadTarget(&State->_history[0], &HasHistory);
    HasHistory = State->_hasHistory;
    WorldRendererCommon_UnloadTarget(&State->_history[1], &HasHistory);
    State->_hasHistory = false;
    State->_isResolved = false;
    State->_hasPreviousFrame = false;
    State->_isTargetTried = false;
    State->_width = 0;
    State->_height = 0;
}

bool WorldRendererClouds_IsAvailable(const WorldRenderer* self)
{
    const WorldRendererCloudState* State = &self->_clouds;
    return (State->_marchShader != NULL) && State->_hasNoise
        && (State->_hasTarget || (State->_quality == CONFIG_CLOUD_QUALITY_OFF));
}

void WorldRendererClouds_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame, double deltaSeconds)
{
    WorldRendererCloudState* State = &self->_clouds;
    WorldCloudsDrift_Advance(&State->_drift, frame->Environment, deltaSeconds);
    WorldClouds_GetFrame(frame->Environment, &State->_drift, frame->Origin, &frame->Clouds);
    frame->AreCloudsActive = WorldRendererClouds_IsAvailable(self) && State->_hasTarget
        && (State->_quality != CONFIG_CLOUD_QUALITY_OFF) && frame->Environment->AreCloudsEnabled && frame->AreAtmosphereTablesActive
        && WorldRendererAtmosphere_IsVolumeAvailable(self) && WorldClouds_HasAny(&frame->Clouds);
    frame->CloudLightShare = 1.0f;
    frame->SkyObjectShare = 1.0f;
    frame->IsCloudShadowActive = false;
}

void WorldRendererClouds_PrepareLight(WorldRenderer* self, WorldRenderFrame* frame, const WorldSkyLightFrame* cloudTop)
{
    WorldRendererCloudState* State = &self->_clouds;
    // The light at the clouds' base decides: the sun's, unless the moon gives more there (the sun far below).
    frame->IsCloudLightMoon = Luminance(cloudTop->Moon) > Luminance(cloudTop->Sun);
    frame->CloudLightDirection = frame->IsCloudLightMoon ? frame->MoonDirection : frame->SunDirection;
    const float* AboveAir = frame->IsCloudLightMoon ? cloudTop->MoonAboveAir : cloudTop->SunAboveAir;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        frame->CloudLightRadiance[Channel] = isfinite(AboveAir[Channel]) ? fmaxf(AboveAir[Channel], 0.0f) : 0.0f;
    }
    frame->IsCloudShadowActive = frame->AreCloudsActive && (State->_shadowShader != NULL) && State->_hasShadowMap
        && (frame->CloudLightDirection.y > 0.0f);
    if (frame->IsCloudShadowActive)
    {
        WorldClouds_GetShadowFrame(&frame->Clouds, frame->Origin, frame->AtmosphereViewHeight,
            frame->CloudLightDirection, SHADOW_MAP_SIZE, &frame->CloudShadow);
    }
}

void WorldRendererClouds_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererCloudState* State = &self->_clouds;
    State->_lastMilliseconds = 0.0;
    if (!frame->AreCloudsActive)
    {
        // The accumulation starts again when they are drawn again.
        State->_hasPreviousFrame = false;
        return;
    }
    Stopwatch Clock = Stopwatch_StartNew();
    if (frame->IsCloudShadowActive)
    {
        RenderShadowMap(self, frame);
    }
    RenderMarch(self, frame);
    State->_isResolved = (State->_resolveShader != NULL) && State->_hasHistory;
    if (State->_isResolved)
    {
        RenderResolve(self, frame);
        State->_previousViewProjection = frame->ViewProjection;
        State->_previousOrigin = frame->Origin;
        State->_hasPreviousFrame = true;
    }
    State->_frameIndex = (State->_frameIndex + 1U) % FRAME_INDEX_WRAP;
    State->_lastMilliseconds = (double)Stopwatch_ElapsedNanoseconds(&Clock) * MILLISECONDS_PER_NANOSECOND;
    Stopwatch_Deconstruct(&Clock);
}

void WorldRendererClouds_BindLayer(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (frame->AreCloudsActive)
    {
        const WorldRendererCloudState* State = &self->_clouds;
        BindTexture2D(WorldRendererTextureUnit_CloudLayer,
            State->_isResolved ? State->_history[State->_newest].texture.id : State->_target.texture.id);
    }
}

void WorldRendererClouds_BindShadow(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (frame->IsCloudShadowActive)
    {
        BindTexture2D(WorldRendererTextureUnit_CloudShadow, self->_clouds._shadowMap.texture.id);
    }
}

void WorldRendererClouds_PrepareLitShader(Shader shader, WorldRendererCloudShadowLocations* outLocations)
{
    outLocations->_locActive = GetShaderLocation(shader, "cloudShadowActive");
    outLocations->_locMap = GetShaderLocation(shader, "cloudShadowMap");
    outLocations->_locFrame = GetShaderLocation(shader, "cloudShadowFrame");
    outLocations->_locLight = GetShaderLocation(shader, "cloudShadowLight");
    outLocations->_locFallback = GetShaderLocation(shader, "cloudShadowFallback");
    outLocations->_locIsMoon = GetShaderLocation(shader, "cloudShadowIsMoon");
    outLocations->_locTop = GetShaderLocation(shader, "cloudShadowTop");
    AssignSamplerUnit(shader, outLocations->_locMap, WorldRendererTextureUnit_CloudShadow);
}

void WorldRendererClouds_UploadLitFrame(Shader shader, const WorldRendererCloudShadowLocations* locations,
    const WorldRenderFrame* frame)
{
    int Active = frame->IsCloudShadowActive ? 1 : 0;
    SetShaderValue(shader, locations->_locActive, &Active, SHADER_UNIFORM_INT);
    if (!frame->IsCloudShadowActive)
    {
        return;
    }
    const WorldCloudShadowFrame* Shadow = &frame->CloudShadow;
    float Frame[4] = { Shadow->Center[0], Shadow->Center[1], 1.0f / Shadow->Extent, Shadow->Plane };
    SetShaderValue(shader, locations->_locFrame, Frame, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locations->_locTop, &Shadow->Top, SHADER_UNIFORM_FLOAT);
    float Light[3] = { frame->CloudLightDirection.x, frame->CloudLightDirection.y, frame->CloudLightDirection.z };
    SetShaderValue(shader, locations->_locLight, Light, SHADER_UNIFORM_VEC3);
    // Beyond the map the light is dimmed by the clouds' share on average (the weather's).
    SetShaderValue(shader, locations->_locFallback, &frame->Weather.DirectShare, SHADER_UNIFORM_FLOAT);
    int IsMoon = frame->IsCloudLightMoon ? 1 : 0;
    SetShaderValue(shader, locations->_locIsMoon, &IsMoon, SHADER_UNIFORM_INT);
}

void WorldRendererClouds_LogStatus(const WorldRenderer* self)
{
    if (self->_logger == NULL)
    {
        return;
    }
    const WorldRendererCloudState* State = &self->_clouds;
    const WorldRendererStatus* Status = &self->_status;
    static const char* const QualityNames[3] = { "off", "low", "high" };
    int32_t Quality = State->_quality;
    const char* QualityName = ((Quality >= 0) && (Quality <= 2)) ? QualityNames[Quality] : "?";
    Error Result = Logger_LogInfoFormatted(self->_logger, (const unsigned char*)u8"WorldRenderer: clouds: %s at %s quality "
        u8"(%dx%d), lit by the %s, shadow map %s; the pass took %.3f ms on the CPU.",
        (Status->Features[WorldRendererFeature_Clouds] == WorldRendererFeatureState_Active) ? "drawn" : "not drawn",
        QualityName, (int)State->_width, (int)State->_height, Status->IsCloudLightMoon ? "moon" : "sun",
        (Status->Features[WorldRendererFeature_CloudShadows] == WorldRendererFeatureState_Active) ? "on" : "off",
        Status->CloudMilliseconds);
    Error_Deconstruct(&Result);
}
