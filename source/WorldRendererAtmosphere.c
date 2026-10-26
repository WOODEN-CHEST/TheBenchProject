// The atmosphere's lookup tables (T15; Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering
// Technique", EGSR 2020), built first in every frame: the transmittance and the multiple scattering when the air changes,
// the sky view every frame. The sky pass, the lit shaders' fog and the stars read them (WorldAtmosphere.h has their
// layout, shaders/common/atmosphere.glsl their GLSL).

#include <inttypes.h>
#include <math.h>
#include "WorldRendererInternal.h"
#include "Logger.h"
#include "raylib/rlgl.h"


// Macros.
/** Asset names of the three table builders (full-screen fragment passes). */
#define TRANSMITTANCE_SHADER_ASSET_NAME ((const unsigned char*)u8"world_atmosphere_transmittance")
#define MULTISCATTER_SHADER_ASSET_NAME ((const unsigned char*)u8"world_atmosphere_multiscatter")
#define SKY_VIEW_SHADER_ASSET_NAME ((const unsigned char*)u8"world_atmosphere_skyview")


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
    *outTarget = WorldRendererCommon_CreateColorTarget(width, height);
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

/* One half of the sky-view table, lit by one light: the shader keeps the half's rows and discards the other's, so every
 * half covers the whole target. Its input tables are bound again for each half: a batch draw forgets them once it is
 * drawn. */
static void DrawSkyViewHalf(WorldRenderer* self, Shader builder, WorldAtmosphereSkyViewHalf half, float lightCosZenith,
    float lightIntensity, const float skyglowRadiance[3])
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    float TableHalf = (float)half;
    SetShaderValue(builder, State->_skyViewLocTableHalf, &TableHalf, SHADER_UNIFORM_FLOAT);
    SetShaderValue(builder, State->_skyViewLocLightCosZenith, &lightCosZenith, SHADER_UNIFORM_FLOAT);
    SetShaderValue(builder, State->_skyViewLocLightIntensity, &lightIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(builder, State->_skyViewLocSkyglowRadiance, skyglowRadiance, SHADER_UNIFORM_VEC3);
    SetShaderValueTexture(builder, State->_skyViewLocations._locTransmittanceTable, State->_transmittance.texture);
    SetShaderValueTexture(builder, State->_skyViewLocations._locMultiscatterTable, State->_multiscatter.texture);
    DrawRectangle(0, 0, State->_skyView.texture.width, State->_skyView.texture.height, WHITE);
    rlDrawRenderBatchActive(); // the next half's uniforms must not reach this one's draw
}

/* The sky-view table: the sun's half every frame (its glow with light pollution's skyglow), the moon's while the
 * moonlit sky shows. */
static void BuildSkyView(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    Shader Builder = GameShader_GetRaylibShader(State->_skyViewShader);
    WorldRendererAtmosphere_UploadFrame(Builder, &State->_skyViewLocations, frame);
    SetShaderValue(Builder, State->_skyViewLocMultipleScattering, &frame->AtmosphereMedium.MultipleScattering,
        SHADER_UNIFORM_FLOAT);

    BeginTable(State->_skyView);
    BeginShaderMode(Builder);
    float SunIntensity = fmaxf(frame->Environment->SunIntensity, 0.0f);
    DrawSkyViewHalf(self, Builder, WorldAtmosphereSkyViewHalf_Sun, frame->SunDirection.y, SunIntensity,
        frame->SkyglowRadiance);
    if (frame->MoonSkyIntensity > 0.0f)
    {
        const float NoSkyglow[3] = { 0.0f, 0.0f, 0.0f };
        DrawSkyViewHalf(self, Builder, WorldAtmosphereSkyViewHalf_Moon, frame->MoonDirection.y, frame->MoonSkyIntensity,
            NoSkyglow);
    }
    EndShaderMode();
    EndTable();
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererAtmosphere_Construct(WorldRenderer* self)
{
    self->_atmosphere = (WorldRendererAtmosphereState){ ._isEnabled = true };
}

void WorldRendererAtmosphere_LoadShaders(WorldRenderer* self)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    State->_transmittanceShader = LoadBuilder(self, TRANSMITTANCE_SHADER_ASSET_NAME, &State->_transmittanceLocations);
    State->_multiscatterShader = LoadBuilder(self, MULTISCATTER_SHADER_ASSET_NAME, &State->_multiscatterLocations);
    State->_skyViewShader = LoadBuilder(self, SKY_VIEW_SHADER_ASSET_NAME, &State->_skyViewLocations);
    if ((State->_transmittanceShader == NULL) || (State->_multiscatterShader == NULL) || (State->_skyViewShader == NULL))
    {
        WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: the atmosphere's tables are unavailable "
            u8"(a table shader is missing): the sky and the fog are marched per pixel, single scattering only.");
        return;
    }

    Shader Multiscatter = GameShader_GetRaylibShader(State->_multiscatterShader);
    State->_multiscatterLocGroundAlbedo = GetShaderLocation(Multiscatter, "groundAlbedo");
    Shader SkyView = GameShader_GetRaylibShader(State->_skyViewShader);
    State->_skyViewLocLightCosZenith = GetShaderLocation(SkyView, "lightCosZenith");
    State->_skyViewLocLightIntensity = GetShaderLocation(SkyView, "lightIntensity");
    State->_skyViewLocMultipleScattering = GetShaderLocation(SkyView, "multipleScattering");
    State->_skyViewLocTableHalf = GetShaderLocation(SkyView, "tableHalf");
    State->_skyViewLocSkyglowRadiance = GetShaderLocation(SkyView, "skyglowRadiance");

    State->_hasTransmittance = CreateTable(WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT,
        &State->_transmittance);
    State->_hasMultiscatter = CreateTable(WORLD_ATMOSPHERE_MULTISCATTER_SIZE, WORLD_ATMOSPHERE_MULTISCATTER_SIZE,
        &State->_multiscatter);
    State->_hasSkyView = CreateTable(WORLD_ATMOSPHERE_SKY_VIEW_WIDTH,
        WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT * WORLD_ATMOSPHERE_SKY_VIEW_HALF_COUNT, &State->_skyView);
    if (!State->_hasTransmittance || !State->_hasMultiscatter || !State->_hasSkyView)
    {
        WorldRendererCommon_WarnOnce(self, &State->_isTargetFailureReported, (const unsigned char*)u8"WorldRenderer: the "
            u8"atmosphere's tables are unavailable (the GPU did not make their RGBA16F targets): the sky and the fog are "
            u8"marched per pixel, single scattering only.");
    }
}

void WorldRendererAtmosphere_Unload(WorldRenderer* self)
{
    WorldRendererAtmosphereState* State = &self->_atmosphere;
    WorldRendererCommon_UnloadTarget(&State->_transmittance, &State->_hasTransmittance);
    WorldRendererCommon_UnloadTarget(&State->_multiscatter, &State->_hasMultiscatter);
    WorldRendererCommon_UnloadTarget(&State->_skyView, &State->_hasSkyView);
    State->_tables.IsBuilt = false;
}

bool WorldRendererAtmosphere_IsAvailable(const WorldRenderer* self)
{
    const WorldRendererAtmosphereState* State = &self->_atmosphere;
    return (State->_transmittanceShader != NULL) && (State->_multiscatterShader != NULL)
        && (State->_skyViewShader != NULL) && State->_hasTransmittance && State->_hasMultiscatter && State->_hasSkyView;
}

void WorldRendererAtmosphere_PrepareFrame(const WorldRenderer* self, WorldRenderFrame* frame)
{
    WorldAtmosphere_GetMedium(frame->Environment, &frame->AtmosphereMedium);
    frame->AtmosphereViewHeight = (float)WorldAtmosphere_GetViewHeight(frame->Origin.Y);
    frame->AreAtmosphereTablesActive = self->_atmosphere._isEnabled && WorldRendererAtmosphere_IsAvailable(self);
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
    BuildSkyView(self, frame);
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

/* Bound manually: the lit passes draw with DrawMesh and the stars with a direct draw, and the sky pass's 2D batch binds
 * only units 0 to 4, so 7 and 8 stay as set here. Unit 0 is made the active one again, where raylib's own binds expect
 * it (a texture loaded later would otherwise land on unit 8). */
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
        ? "on (the sky and the fog's sky from them, with multiple scattering)"
        : ((State == WorldRendererFeatureState_Unavailable) ? "unavailable (the sky and the fog are marched per pixel)"
            : "off (the A/B: the sky and the fog marched per pixel, single scattering)");
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: atmosphere tables %s: transmittance %d x %d, multiple scattering %d x %d "
        u8"(built %" PRIu32 u8" times, whenever the air changes), sky view %d x %d for the sun and %d x %d for the moon "
        u8"(every frame; the moon's while its sky shows); the sky seen from %.2f m above sea level; the last render took "
        u8"%.2f ms on the CPU.",
        Tables, WORLD_ATMOSPHERE_TRANSMITTANCE_WIDTH, WORLD_ATMOSPHERE_TRANSMITTANCE_HEIGHT,
        WORLD_ATMOSPHERE_MULTISCATTER_SIZE, WORLD_ATMOSPHERE_MULTISCATTER_SIZE, Status->AtmosphereTableBuildCount,
        WORLD_ATMOSPHERE_SKY_VIEW_WIDTH, WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT, WORLD_ATMOSPHERE_SKY_VIEW_WIDTH,
        WORLD_ATMOSPHERE_SKY_VIEW_HEIGHT, (double)Status->AtmosphereViewHeight, Status->RenderMilliseconds);
    Error_Deconstruct(&LogResult);
}
