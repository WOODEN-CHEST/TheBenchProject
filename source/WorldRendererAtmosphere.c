// The atmosphere's lookup tables (Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering
// Technique", EGSR 2020), built first in every frame: the transmittance and the multiple scattering when the air changes,
// the sky's irradiance after them, the sky view every frame; and the aerial-perspective volume built from them every
// frame, whose last row holds the sky's light by height. The sky pass and the stars read the tables; every
// lit surface, the sky pass's ground haze and the outlines read the volume (WorldAtmosphere.h has their layout,
// shaders/common/atmosphere.glsl and shaders/common/haze.glsl their GLSL).

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "Logger.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"
#include "wr/WRStopwatch.h"


// Macros.
/** Asset names of the three table builders and the volume's (full-screen fragment passes). */
#define TRANSMITTANCE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_atmosphere_transmittance")
#define MULTISCATTER_SHADER_ASSET_NAME ((const unsigned char*)u8"world_atmosphere_multiscatter")
#define SKY_VIEW_SHADER_ASSET_NAME ((const unsigned char*)u8"world_atmosphere_skyview")
#define VOLUME_SHADER_ASSET_NAME ((const unsigned char*)u8"world_atmosphere_volume")
#define IRRADIANCE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_atmosphere_irradiance")
/** How long a frame spends on a rebuild of the C mirror's tables when the air changes, in nanoseconds: a step
 *  (WorldSkyLight_RunBuildStep, at most about 0.75 ms) runs while the time so far and the last step's fit in it; the
 *  first always runs. */
#define SKY_LIGHT_BUILD_BUDGET_NANOSECONDS 1000000ULL


// Types.
/** The light one half of the sky-view table is lit by, and the far ground's light in it. */
typedef struct SkyViewLightStruct
{
    /** Which half, the light's zenith cosine and its light above the air (multiples of WORLD_ATMOSPHERE_SUN_LIGHT, colour
     *  in: the frame's AirSunIntensity or AirMoonIntensity). */
    WorldAtmosphereSkyViewHalf Half;
    float LightCosZenith;
    const float* LightIntensity;
    /** Light pollution's light in the air (the sun's half only; linear). */
    const float* LightPollution;
    /** The moonless night's own light on the far ground per unit of albedo (the sun's half only). */
    const float* GroundNightLight;
} SkyViewLight;


// Static functions.
/* Points a sampler uniform at a fixed texture unit, once at load (raylib's SetShaderValue ignores location -1). */
static void AssignSamplerUnit(Shader shader, int location, WorldRendererTextureUnit unit)
{
    int UnitIndex = (int)unit;
    SetShaderValue(shader, location, &UnitIndex, SHADER_UNIFORM_INT);
}

/* Creates one table's target (RGBA16F colour, bilinear, clamped to its edges). false when the GPU cannot make it. */
static bool CreateTable(int width, int height, RenderTexture2D* outTarget)
{
    *outTarget = WorldRendererCommon_CreateColorTarget(width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    if (outTarget->id == 0)
    {
        return false;
    }
    SetTextureFilter(outTarget->texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(outTarget->texture, TEXTURE_WRAP_CLAMP);
    return true;
}

/* Loads one table builder and its atmosphere locations; NULL when it is unavailable (logged by the loader). */
static GameShader* LoadBuilder(WorldRenderer* self, const unsigned char* assetName,
    WorldRendererAtmosphereLocations* outLocations)
{
    GameShader* Builder = WorldRendererCommon_LoadShader(self, assetName);
    if (Builder != NULL)
    {
        WorldRendererAtmosphere_PrepareShader(GameShader_GetRaylibShader(Builder), outLocations);
    }
    return Builder;
}

/* Starts drawing into a table with blending off: every texel is written as the builder computes it. The default
 * blending would mix in the old texel, and on drivers that blend by IEEE rules (Mesa's) an old NaN or infinity, from a
 * frame with an absurd setting, would stay in the table for good, even weighted by 0. */
static void BeginTable(RenderTexture2D target)
{
    BeginTextureMode(target); // flushes the batch first, so the blending change below affects the table's draws only
    rlDisableColorBlend();
}

/* Ends drawing into a table (after EndShaderMode, which drew the batch unblended) and restores the default blending for
 * the passes that follow. */
static void EndTable(void)
{
    rlEnableColorBlend();
    EndTextureMode();
}

/* Draws a full-screen pass of @p shader into @p target (every texel). */
static void DrawTable(RenderTexture2D target, Shader shader)
{
    BeginTable(target);
    BeginShaderMode(shader);
    DrawRectangle(0, 0, target.texture.width, target.texture.height, WHITE);
    EndShaderMode();
    EndTable();
}

/* The transmittance table: the share of light that crosses the air from each height and zenith angle to the top. */
static void BuildTransmittance(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    Shader Builder = GameShader_GetRaylibShader(State->_transmittanceShader);
    WorldRendererAtmosphere_UploadFrame(Builder, &State->_transmittanceLocations, frame);
    DrawTable(State->_transmittance, Builder);
}

/* The multiple scattering table, from the transmittance table and the ground's albedo. */
static void BuildMultiscatter(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    Shader Builder = GameShader_GetRaylibShader(State->_multiscatterShader);
    WorldRendererAtmosphere_UploadFrame(Builder, &State->_multiscatterLocations, frame);
    SetShaderValue(Builder, State->_multiscatterLocGroundAlbedo, frame->AtmosphereMedium.GroundAlbedo,
        SHADER_UNIFORM_VEC3);
    BeginTable(State->_multiscatter);
    BeginShaderMode(Builder);
    // A 2D (batch) draw binds its input tables through SetShaderValueTexture, after BeginShaderMode.
    SetShaderValueTexture(Builder, State->_multiscatterLocations._locTransmittanceTable, State->_transmittance.texture);
    DrawRectangle(0, 0, State->_multiscatter.texture.width, State->_multiscatter.texture.height, WHITE);
    EndShaderMode();
    EndTable();
}

/* The sky's irradiance table, from the transmittance and multiple scattering tables. */
static void BuildIrradiance(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    Shader Builder = GameShader_GetRaylibShader(State->_irradianceShader);
    WorldRendererAtmosphere_UploadFrame(Builder, &State->_irradianceLocations, frame);
    SetShaderValue(Builder, State->_irradianceLocMultipleScattering, &frame->AtmosphereMedium.MultipleScattering,
        SHADER_UNIFORM_FLOAT);
    BeginTable(State->_irradiance);
    BeginShaderMode(Builder);
    SetShaderValueTexture(Builder, State->_irradianceLocations._locTransmittanceTable, State->_transmittance.texture);
    SetShaderValueTexture(Builder, State->_irradianceLocations._locMultiscatterTable, State->_multiscatter.texture);
    DrawRectangle(0, 0, State->_irradiance.texture.width, State->_irradiance.texture.height, WHITE);
    EndShaderMode();
    EndTable();
}

/* One half of the sky-view table, lit by one light: the shader keeps the half's rows and discards the other's, so every
 * half covers the whole target. Its input tables are bound again for each half: a batch draw forgets them once it is
 * drawn. */
static void DrawSkyViewHalf(WorldRenderer* self, Shader builder, const SkyViewLight* light)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    float TableHalf = (float)light->Half;
    SetShaderValue(builder, State->_skyViewLocTableHalf, &TableHalf, SHADER_UNIFORM_FLOAT);
    SetShaderValue(builder, State->_skyViewLocLightCosZenith, &light->LightCosZenith, SHADER_UNIFORM_FLOAT);
    SetShaderValue(builder, State->_skyViewLocLightIntensity, light->LightIntensity, SHADER_UNIFORM_VEC3);
    SetShaderValue(builder, State->_skyViewLocLightPollution, light->LightPollution, SHADER_UNIFORM_VEC3);
    SetShaderValue(builder, State->_skyViewLocGroundNightLight, light->GroundNightLight, SHADER_UNIFORM_VEC3);
    SetShaderValueTexture(builder, State->_skyViewLocations._locTransmittanceTable, State->_transmittance.texture);
    SetShaderValueTexture(builder, State->_skyViewLocations._locMultiscatterTable, State->_multiscatter.texture);
    SetShaderValueTexture(builder, State->_skyViewLocations._locIrradianceTable, State->_irradiance.texture);
    DrawRectangle(0, 0, State->_skyView.texture.width, State->_skyView.texture.height, WHITE);
    rlDrawRenderBatchActive(); // the next half's uniforms must not reach this one's draw
}

/* The sky-view table: the sun's half every frame (its glow with light pollution's light, the far ground lit by the sun,
 * the sky's irradiance and the night's light), the moon's while the moonlit sky shows (the far ground lit by the moon
 * and its sky). The far ground is lit as the lit shaders light the terrain (the shader: through the air at the far
 * point), so it meets a terrain's edge without a seam. */
static void BuildSkyView(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    const WorldEnvironment* Environment = frame->Environment;
    Shader Builder = GameShader_GetRaylibShader(State->_skyViewShader);
    WorldRendererAtmosphere_UploadFrame(Builder, &State->_skyViewLocations, frame);
    SetShaderValue(Builder, State->_skyViewLocMultipleScattering, &frame->AtmosphereMedium.MultipleScattering,
        SHADER_UNIFORM_FLOAT);
    float SkyTint[3];
    WorldRendererCommon_ColorToLinear(Environment->SkyTint, SkyTint);
    SetShaderValue(Builder, State->_skyViewLocSkyTint, SkyTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(Builder, State->_skyViewLocHazeDistanceScale, &frame->Haze.DistanceScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Builder, State->_skyViewLocHazeStartDistance, &frame->Haze.StartDistance, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Builder, State->_skyViewLocGroundAlbedo, frame->AtmosphereMedium.GroundAlbedo, SHADER_UNIFORM_VEC3);
    SetShaderValue(Builder, State->_skyViewLocAmbientTint, frame->SkyLight.AmbientTint, SHADER_UNIFORM_VEC3);

    BeginTable(State->_skyView);
    BeginShaderMode(Builder);
    const float None[3] = { 0.0f, 0.0f, 0.0f };
    SkyViewLight Sun =
    {
        .Half = WorldAtmosphereSkyViewHalf_Sun, .LightCosZenith = frame->SunDirection.y,
        .LightIntensity = frame->AirSunIntensity, .LightPollution = frame->LightPollutionRadiance,
        .GroundNightLight = frame->SkyLight.Night
    };
    DrawSkyViewHalf(self, Builder, &Sun);
    if (frame->MoonSkyIntensity > 0.0f)
    {
        SkyViewLight Moon =
        {
            .Half = WorldAtmosphereSkyViewHalf_Moon, .LightCosZenith = frame->MoonDirection.y,
            .LightIntensity = frame->AirMoonIntensity, .LightPollution = None, .GroundNightLight = None
        };
        DrawSkyViewHalf(self, Builder, &Moon);
    }
    EndShaderMode();
    EndTable();
}

/* The aerial-perspective volume: the air's in-scatter and transmittance for the camera's screen directions at the
 * volume's slices, lit by the sun, the moon (while the moonlit sky shows) and light pollution, and its last row, the
 * frame's values: the light at the ground haze's base and the sky's light by height. Only the last row while the
 * haze is off (frame->IsHazeActive false, frame->IsSkyLightPerHeight true): the lit shaders read nothing else then. */
static void BuildVolume(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    const WorldEnvironment* Environment = frame->Environment;
    Shader Builder = GameShader_GetRaylibShader(State->_volumeShader);
    WorldRendererAtmosphere_UploadFrame(Builder, &State->_volumeLocations, frame);
    SetShaderValueMatrix(Builder, State->_volumeLocInvViewProjection, frame->InvViewProjection);
    float SunDirection[3] = { frame->SunDirection.x, frame->SunDirection.y, frame->SunDirection.z };
    SetShaderValue(Builder, State->_volumeLocSunDirection, SunDirection, SHADER_UNIFORM_VEC3);
    SetShaderValue(Builder, State->_volumeLocSunIntensity, frame->AirSunIntensity, SHADER_UNIFORM_VEC3);
    float MoonDirection[3] = { frame->MoonDirection.x, frame->MoonDirection.y, frame->MoonDirection.z };
    SetShaderValue(Builder, State->_volumeLocMoonDirection, MoonDirection, SHADER_UNIFORM_VEC3);
    SetShaderValue(Builder, State->_volumeLocMoonIntensity, frame->AirMoonIntensity, SHADER_UNIFORM_VEC3);
    SetShaderValue(Builder, State->_volumeLocMultipleScattering, &frame->AtmosphereMedium.MultipleScattering,
        SHADER_UNIFORM_FLOAT);
    SetShaderValue(Builder, State->_volumeLocLightPollution, frame->LightPollutionRadiance, SHADER_UNIFORM_VEC3);
    float SkyTint[3];
    WorldRendererCommon_ColorToLinear(Environment->SkyTint, SkyTint);
    SetShaderValue(Builder, State->_volumeLocSkyTint, SkyTint, SHADER_UNIFORM_VEC3);
    float BaseHeight = frame->Haze.GroundHazeBase - frame->Haze.GroundHeight;
    SetShaderValue(Builder, State->_volumeLocGroundHazeBaseHeight, &BaseHeight, SHADER_UNIFORM_FLOAT);
    SetShaderValue(Builder, State->_volumeLocGroundAlbedo, frame->AtmosphereMedium.GroundAlbedo, SHADER_UNIFORM_VEC3);

    BeginTable(State->_volume);
    BeginShaderMode(Builder);
    SetShaderValueTexture(Builder, State->_volumeLocations._locTransmittanceTable, State->_transmittance.texture);
    SetShaderValueTexture(Builder, State->_volumeLocations._locMultiscatterTable, State->_multiscatter.texture);
    SetShaderValueTexture(Builder, State->_volumeLocations._locIrradianceTable, State->_irradiance.texture);
    // The values' row alone is the draw's first row: raylib's texture mode draws y = 0 into the target's highest row
    // (the shader's gl_FragCoord.y = 2 x WORLD_ATMOSPHERE_VOLUME_SIZE).
    int Rows = frame->IsHazeActive ? State->_volume.texture.height : 1;
    DrawRectangle(0, 0, State->_volume.texture.width, Rows, WHITE);
    EndShaderMode();
    EndTable();
}

/* The volume builder's own uniform locations. */
static void CacheVolumeLocations(WorldRendererAtmosphereState* state)
{
    Shader Volume = GameShader_GetRaylibShader(state->_volumeShader);
    state->_volumeLocInvViewProjection = GetShaderLocation(Volume, "invViewProjection");
    state->_volumeLocSunDirection = GetShaderLocation(Volume, "sunDirection");
    state->_volumeLocSunIntensity = GetShaderLocation(Volume, "sunIntensity");
    state->_volumeLocMoonDirection = GetShaderLocation(Volume, "moonDirection");
    state->_volumeLocMoonIntensity = GetShaderLocation(Volume, "moonIntensity");
    state->_volumeLocMultipleScattering = GetShaderLocation(Volume, "multipleScattering");
    state->_volumeLocLightPollution = GetShaderLocation(Volume, "lightPollutionRadiance");
    state->_volumeLocSkyTint = GetShaderLocation(Volume, "skyTint");
    state->_volumeLocGroundHazeBaseHeight = GetShaderLocation(Volume, "groundHazeBaseHeight");
    state->_volumeLocGroundAlbedo = GetShaderLocation(Volume, "groundAlbedo");
}

/* The sky-view builder's own uniform locations. */
static void CacheSkyViewLocations(WorldRendererAtmosphereState* state)
{
    Shader SkyView = GameShader_GetRaylibShader(state->_skyViewShader);
    state->_skyViewLocLightCosZenith = GetShaderLocation(SkyView, "lightCosZenith");
    state->_skyViewLocLightIntensity = GetShaderLocation(SkyView, "lightIntensity");
    state->_skyViewLocMultipleScattering = GetShaderLocation(SkyView, "multipleScattering");
    state->_skyViewLocTableHalf = GetShaderLocation(SkyView, "tableHalf");
    state->_skyViewLocLightPollution = GetShaderLocation(SkyView, "lightPollutionRadiance");
    state->_skyViewLocSkyTint = GetShaderLocation(SkyView, "skyTint");
    state->_skyViewLocHazeDistanceScale = GetShaderLocation(SkyView, "hazeDistanceScale");
    state->_skyViewLocHazeStartDistance = GetShaderLocation(SkyView, "hazeStartDistance");
    state->_skyViewLocGroundAlbedo = GetShaderLocation(SkyView, "groundAlbedo");
    state->_skyViewLocAmbientTint = GetShaderLocation(SkyView, "ambientTint");
    state->_skyViewLocGroundNightLight = GetShaderLocation(SkyView, "groundNightLight");
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererAtmosphere_Construct(WorldRenderer* self)
{
    self->_atmosphere = (WorldRendererAtmosphereState){ ._isEnabled = true, ._view = WorldRendererHazeView_Off };
    // The C mirror's two table sets, about 85 KB each: on the heap, with the renderer (built when a world is
    // prepared, or on the first frame).
    WorldSkyLight_ConstructBuilder(&self->_atmosphere._skyLight);
}

void WorldRendererAtmosphere_Deconstruct(WorldRenderer* self)
{
    WorldSkyLight_DeconstructBuilder(&self->_atmosphere._skyLight);
}

/* A rebuild fills the second set for the air it started with, the old set lighting the frames meanwhile; a change during
 * a rebuild waits for the next one (WorldSkyLight_UpdateBuilder). */
void WorldRendererAtmosphere_UpdateSkyLightTables(WorldRenderer* self, const WorldAtmosphereMedium* medium)
{
    WorldSkyLightBuilder* Builder = &self->_atmosphere._skyLight;
    if (!WorldSkyLight_UpdateBuilder(Builder, medium))
    {
        return;
    }
    Stopwatch Clock = Stopwatch_StartNew();
    uint64_t StepStart = 0U;
    uint64_t Now = 0U;
    bool HasMore = true;
    do
    {
        StepStart = Now;
        HasMore = WorldSkyLight_RunBuildStep(Builder);
        Now = Stopwatch_ElapsedNanoseconds(&Clock);
    }
    while (HasMore && (Now + (Now - StepStart) <= SKY_LIGHT_BUILD_BUDGET_NANOSECONDS));
    Stopwatch_Deconstruct(&Clock);
}

void WorldRendererAtmosphere_LoadShaders(WorldRenderer* self)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    State->_transmittanceShader = LoadBuilder(self, TRANSMITTANCE_SHADER_ASSET_NAME, &State->_transmittanceLocations);
    State->_multiscatterShader = LoadBuilder(self, MULTISCATTER_SHADER_ASSET_NAME, &State->_multiscatterLocations);
    State->_skyViewShader = LoadBuilder(self, SKY_VIEW_SHADER_ASSET_NAME, &State->_skyViewLocations);
    State->_irradianceShader = LoadBuilder(self, IRRADIANCE_SHADER_ASSET_NAME, &State->_irradianceLocations);
    if ((State->_transmittanceShader == NULL) || (State->_multiscatterShader == NULL) || (State->_skyViewShader == NULL)
        || (State->_irradianceShader == NULL))
    {
        WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: the atmosphere's tables are unavailable "
            u8"(a table shader is missing): the sky is marched per pixel, single scattering only, and nothing is hazed.");
        return;
    }

    Shader Multiscatter = GameShader_GetRaylibShader(State->_multiscatterShader);
    State->_multiscatterLocGroundAlbedo = GetShaderLocation(Multiscatter, "groundAlbedo");
    CacheSkyViewLocations(State);
    State->_irradianceLocMultipleScattering = GetShaderLocation(GameShader_GetRaylibShader(State->_irradianceShader),
        "multipleScattering");

    State->_hasTransmittance = CreateTable(WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT,
        &State->_transmittance);
    State->_hasMultiscatter = CreateTable(WORLD_ATMOSPHERE_MULTISCATTER_SIZE, WORLD_ATMOSPHERE_MULTISCATTER_SIZE,
        &State->_multiscatter);
    State->_hasSkyView = CreateTable(WORLD_ATMOSPHERE_SKY_VIEW_WIDTH,
        WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT * WORLD_ATMOSPHERE_SKY_VIEW_HALF_COUNT, &State->_skyView);
    State->_hasIrradiance = CreateTable(WORLD_ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH, WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT,
        &State->_irradiance);
    if (!State->_hasTransmittance || !State->_hasMultiscatter || !State->_hasSkyView || !State->_hasIrradiance)
    {
        WorldRendererCommon_WarnOnce(self, &State->_isTargetFailureReported, (const unsigned char*)u8"WorldRenderer: the "
            u8"atmosphere's tables are unavailable (the GPU did not make their RGBA16F targets): the sky is marched per "
            u8"pixel, single scattering only, and nothing is hazed.");
        return;
    }

    // The volume, from the tables.
    State->_volumeShader = LoadBuilder(self, VOLUME_SHADER_ASSET_NAME, &State->_volumeLocations);
    if (State->_volumeShader == NULL)
    {
        WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: the haze is unavailable (its volume's "
            u8"shader is missing): distant things show without aerial perspective.");
        return;
    }
    CacheVolumeLocations(State);
    State->_hasVolume = CreateTable(WORLD_ATMOSPHERE_VOLUME_WIDTH, WORLD_ATMOSPHERE_VOLUME_HEIGHT, &State->_volume);
    if (!State->_hasVolume)
    {
        WorldRendererCommon_WarnOnce(self, &State->_isTargetFailureReported, (const unsigned char*)u8"WorldRenderer: the "
            u8"haze is unavailable (the GPU did not make its volume's RGBA16F target): distant things show without aerial "
            u8"perspective.");
    }
}

void WorldRendererAtmosphere_Unload(WorldRenderer* self)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    WorldRendererCommon_UnloadTarget(&State->_transmittance, &State->_hasTransmittance);
    WorldRendererCommon_UnloadTarget(&State->_multiscatter, &State->_hasMultiscatter);
    WorldRendererCommon_UnloadTarget(&State->_skyView, &State->_hasSkyView);
    WorldRendererCommon_UnloadTarget(&State->_volume, &State->_hasVolume);
    WorldRendererCommon_UnloadTarget(&State->_irradiance, &State->_hasIrradiance);
    State->_tables.IsBuilt = false;
    State->_irradianceTable.IsBuilt = false;
}

bool WorldRendererAtmosphere_IsAvailable(const WorldRenderer* self)
{
    const WorldRendererAtmosphereState* State = &self->_atmosphere;
    return (State->_transmittanceShader != NULL) && (State->_multiscatterShader != NULL)
        && (State->_skyViewShader != NULL) && (State->_irradianceShader != NULL) && State->_hasTransmittance
        && State->_hasMultiscatter && State->_hasSkyView && State->_hasIrradiance;
}

bool WorldRendererAtmosphere_IsVolumeAvailable(const WorldRenderer* self)
{
    return WorldRendererAtmosphere_IsAvailable(self) && (self->_atmosphere._volumeShader != NULL)
        && self->_atmosphere._hasVolume;
}

void WorldRendererAtmosphere_PrepareFrame(const WorldRenderer* self, WorldRenderFrame* frame)
{
    WorldAtmosphere_GetMedium(frame->Environment, &frame->AtmosphereMedium);
    WorldAtmosphere_GetHaze(frame->Environment, WorldRendererCommon_ConfigFogStrength(self), &frame->Haze);
    WorldAtmosphere_GetLightPollutionRadiance(frame->Environment, frame->Daylight, frame->LightPollutionRadiance);
    // The sky is seen from the camera's height above the atmosphere's ground (horizon_ground_height), in double.
    frame->AtmosphereViewHeight = (float)WorldAtmosphere_GetViewHeight(frame->Origin.Y - (double)frame->Haze.GroundHeight);
    frame->AreAtmosphereTablesActive = self->_atmosphere._isEnabled && WorldRendererAtmosphere_IsAvailable(self);
    frame->IsHazeActive = frame->AreAtmosphereTablesActive && WorldRendererAtmosphere_IsVolumeAvailable(self)
        && (frame->Haze.DistanceScale > 0.0f);
}

void WorldRendererAtmosphere_Render(WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->AreAtmosphereTablesActive)
    {
        return;
    }
    if (WorldAtmosphere_UpdateTableRecord(&self->_atmosphere._tables, &frame->AtmosphereMedium))
    {
        BuildTransmittance(self, frame);
        BuildMultiscatter(self, frame);
    }
    if (WorldAtmosphere_UpdateIrradianceRecord(&self->_atmosphere._irradianceTable, &frame->AtmosphereMedium))
    {
        BuildIrradiance(self, frame);
    }
    BuildSkyView(self, frame);
    if (frame->IsHazeActive || frame->IsSkyLightPerHeight)
    {
        BuildVolume(self, frame);
    }
    WorldRendererAtmosphere_BindTables(self, frame);
}

void WorldRendererAtmosphere_PrepareShader(Shader shader, WorldRendererAtmosphereLocations* outLocations)
{
    outLocations->_locRayleighScattering = GetShaderLocation(shader, "atmosphereRayleighScattering");
    outLocations->_locMieScattering = GetShaderLocation(shader, "atmosphereMieScattering");
    outLocations->_locMieExtinction = GetShaderLocation(shader, "atmosphereMieExtinction");
    outLocations->_locMieHeight = GetShaderLocation(shader, "atmosphereMieHeight");
    outLocations->_locMieAnisotropy = GetShaderLocation(shader, "atmosphereMieAnisotropy");
    outLocations->_locOzoneAbsorption = GetShaderLocation(shader, "atmosphereOzoneAbsorption");
    outLocations->_locViewHeight = GetShaderLocation(shader, "atmosphereViewHeight");
    outLocations->_locTablesActive = GetShaderLocation(shader, "atmosphereTablesActive");
    outLocations->_locTransmittanceTable = GetShaderLocation(shader, "atmosphereTransmittanceTable");
    outLocations->_locMultiscatterTable = GetShaderLocation(shader, "atmosphereMultiscatterTable");
    outLocations->_locSkyViewTable = GetShaderLocation(shader, "atmosphereSkyViewTable");
    outLocations->_locIrradianceTable = GetShaderLocation(shader, "atmosphereIrradianceTable"); // the builders' only
    // The readers' tables sit on fixed units for the whole frame (the builders bind theirs per draw instead).
    AssignSamplerUnit(shader, outLocations->_locTransmittanceTable, WorldRendererTextureUnit_AtmosphereTransmittance);
    AssignSamplerUnit(shader, outLocations->_locSkyViewTable, WorldRendererTextureUnit_AtmosphereSkyView);
}

void WorldRendererAtmosphere_UploadFrame(Shader shader, const WorldRendererAtmosphereLocations* locations,
    const WorldRenderFrame* frame)
{
    const WorldAtmosphereMedium* Medium = &frame->AtmosphereMedium;
    SetShaderValue(shader, locations->_locRayleighScattering, Medium->RayleighScattering, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locMieScattering, Medium->MieScattering, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locMieExtinction, Medium->MieExtinction, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locMieHeight, &Medium->MieHeight, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locMieAnisotropy, &Medium->MieAnisotropy, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locOzoneAbsorption, Medium->OzoneAbsorption, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locViewHeight, &frame->AtmosphereViewHeight, SHADER_UNIFORM_FLOAT);
    int TablesActive = frame->AreAtmosphereTablesActive ? 1 : 0;
    SetShaderValue(shader, locations->_locTablesActive, &TablesActive, SHADER_UNIFORM_INT);
}

/* Bound manually: the stars draw with a direct draw, and the sky pass's 2D batch binds only units 0 to 4, so 7 and 8 stay
 * as set here until the lit passes bind the sky's occupancy volume on 7 (WorldRendererSkyVisibility_BindMap) and the haze's
 * volume on 8 (WorldRendererAtmosphere_BindVolume). Unit 0 is made the active
 * one again, where raylib's own binds expect it (a texture loaded later would otherwise land on unit 8). */
void WorldRendererAtmosphere_BindTables(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->AreAtmosphereTablesActive)
    {
        return;
    }
    rlActiveTextureSlot(WorldRendererTextureUnit_AtmosphereTransmittance);
    rlEnableTexture(self->_atmosphere._transmittance.texture.id);
    rlActiveTextureSlot(WorldRendererTextureUnit_AtmosphereSkyView);
    rlEnableTexture(self->_atmosphere._skyView.texture.id);
    rlActiveTextureSlot(0);
}

void WorldRendererAtmosphere_PrepareHazeShader(Shader shader, bool isVolumeOnFixedUnit,
    WorldRendererHazeLocations* outLocations)
{
    outLocations->_locActive = GetShaderLocation(shader, "hazeActive");
    outLocations->_locVolume = GetShaderLocation(shader, "atmosphereVolume");
    outLocations->_locDistanceScale = GetShaderLocation(shader, "hazeDistanceScale");
    outLocations->_locStartDistance = GetShaderLocation(shader, "hazeStartDistance");
    outLocations->_locGroundHazeDensity = GetShaderLocation(shader, "groundHazeDensity");
    outLocations->_locGroundHazeThickness = GetShaderLocation(shader, "groundHazeThickness");
    outLocations->_locGroundHazeHeight = GetShaderLocation(shader, "groundHazeHeight");
    outLocations->_locGroundHazeAlbedo = GetShaderLocation(shader, "groundHazeAlbedo");
    outLocations->_locGroundHazeGlow = GetShaderLocation(shader, "groundHazeGlow");
    if (isVolumeOnFixedUnit)
    {
        AssignSamplerUnit(shader, outLocations->_locVolume, WorldRendererTextureUnit_HazeVolume);
    }
}

void WorldRendererAtmosphere_UploadHaze(Shader shader, const WorldRendererHazeLocations* locations,
    const WorldRenderFrame* frame)
{
    const WorldAtmosphereHaze* Haze = &frame->Haze;
    int Active = frame->IsHazeActive ? 1 : 0;
    SetShaderValue(shader, locations->_locActive, &Active, SHADER_UNIFORM_INT);
    SetShaderValue(shader, locations->_locDistanceScale, &Haze->DistanceScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locStartDistance, &Haze->StartDistance, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locGroundHazeDensity, &Haze->GroundHazeDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locGroundHazeThickness, &Haze->GroundHazeThickness, SHADER_UNIFORM_FLOAT);
    // The camera's height above the haze's base, in double (the camera may be far from the world's origin).
    float Height = (float)(frame->Origin.Y - (double)Haze->GroundHazeBase);
    SetShaderValue(shader, locations->_locGroundHazeHeight, &Height, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locations->_locGroundHazeAlbedo, Haze->GroundHazeAlbedo, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locGroundHazeGlow, &Haze->GroundHazeGlow, SHADER_UNIFORM_FLOAT);
}

/* Bound manually, as the tables are (WorldRendererAtmosphere_BindTables): the lit passes draw with DrawMesh. Bound
 * while it holds the haze or the sky's light by height (its last row). */
void WorldRendererAtmosphere_BindVolume(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->IsHazeActive && !frame->IsSkyLightPerHeight)
    {
        return;
    }
    rlActiveTextureSlot(WorldRendererTextureUnit_HazeVolume);
    rlEnableTexture(self->_atmosphere._volume.texture.id);
    rlActiveTextureSlot(0);
}

void WorldRendererAtmosphere_BindVolumeTexture(const WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererHazeLocations* locations)
{
    if (frame->IsHazeActive)
    {
        SetShaderValueTexture(shader, locations->_locVolume, self->_atmosphere._volume.texture);
    }
}

/* The slice's tile, half a texel in from its edges so the bilinear reads stay in it, drawn flipped (its rows run from
 * the screen's bottom up, as the scene target's do) over the whole scene target. */
void WorldRendererAtmosphere_DrawView(WorldRenderer* self, const WorldRenderFrame* frame)
{
    const WorldRendererAtmosphereState* State = &self->_atmosphere;
    if ((State->_view == WorldRendererHazeView_Off) || !frame->IsHazeActive)
    {
        return;
    }
    float Size = (float)WORLD_ATMOSPHERE_VOLUME_SIZE;
    float Part = (State->_view == WorldRendererHazeView_Transmittance)
        ? (float)WorldAtmosphereVolumePart_Transmittance : (float)WorldAtmosphereVolumePart_InScatter;
    Rectangle Source = { (float)State->_viewSlice * Size + 0.5f, Part * Size + 0.5f, Size - 1.0f, -(Size - 1.0f) };
    Rectangle Destination = { 0.0f, 0.0f, (float)frame->SceneWidth, (float)frame->SceneHeight };
    BeginTextureMode(self->_scene._target);
    DrawTexturePro(State->_volume.texture, Source, Destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndTextureMode();
}

void WorldRendererAtmosphere_LogStatus(const WorldRenderer* self)
{
    if (self->_logger == NULL)
    {
        return;
    }
    if (!self->_isStatusReported)
    {
        Error NoRenderResult = Logger_LogInfo(self->_logger,
            (const unsigned char*)u8"WorldRenderer: atmosphere tables: no render yet.");
        Error_Deconstruct(&NoRenderResult);
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    WorldRendererFeatureState State = Status->Features[WorldRendererFeature_AtmosphereTables];
    const char* Tables = (State == WorldRendererFeatureState_Active)
        ? "on (the sky and the far ground from them, with multiple scattering)"
        : ((State == WorldRendererFeatureState_Unavailable) ? "unavailable (the sky is marched per pixel)"
            : "off (the A/B: the sky marched per pixel, single scattering)");
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: atmosphere tables %s: transmittance %d x %d, multiple scattering %d x %d "
        u8"(built %" PRIu32 u8" times, whenever the air changes), the sky's irradiance %d x %d (built %" PRIu32 u8" times, "
        u8"after them or when the haze's forward scattering or the multiple scattering's share changes), sky view %d x %d "
        u8"for the sun and %d x %d for the moon (every frame; the moon's while its sky shows); the sky seen from %.2f m above "
        u8"the atmosphere's ground; the last render took %.2f ms on the CPU.",
        Tables, WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT,
        WORLD_ATMOSPHERE_MULTISCATTER_SIZE, WORLD_ATMOSPHERE_MULTISCATTER_SIZE, Status->AtmosphereTableBuildCount,
        WORLD_ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH, WORLD_ATMOSPHERE_IRRADIANCE_HEIGHT, Status->IrradianceTableBuildCount,
        WORLD_ATMOSPHERE_SKY_VIEW_WIDTH, WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT, WORLD_ATMOSPHERE_SKY_VIEW_WIDTH,
        WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT, (double)Status->AtmosphereViewHeight, Status->RenderMilliseconds);
    Error_Deconstruct(&LogResult);

    WorldRendererFeatureState HazeState = Status->Features[WorldRendererFeature_Haze];
    const char* Haze = (HazeState == WorldRendererFeatureState_Active) ? "on"
        : ((HazeState == WorldRendererFeatureState_Unavailable) ? "unavailable (nothing is hazed)"
            : "off (nothing is hazed: the tables are off, or the haze strength is 0)");
    char View[64] = "";
    if (Status->HazeView != WorldRendererHazeView_Off)
    {
        snprintf(View, sizeof(View), "; showing slice %d's %s", Status->HazeViewSlice,
            (const char*)WorldRenderer_GetHazeViewName(Status->HazeView));
    }
    Error HazeResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: haze %s: volume %d x %d directions at %d slices (%.0f m to %.0f km, then "
        u8"the whole ray), distances x %.3g; ground haze %.4g per metre at its base%s.",
        Haze, WORLD_ATMOSPHERE_VOLUME_SIZE, WORLD_ATMOSPHERE_VOLUME_SIZE, WORLD_ATMOSPHERE_VOLUME_SLICE_COUNT,
        WORLD_ATMOSPHERE_VOLUME_NEAREST, WORLD_ATMOSPHERE_VOLUME_FARTHEST / 1000.0, (double)Status->HazeDistanceScale,
        (double)Status->GroundHazeDensity, View);
    Error_Deconstruct(&HazeResult);
}
