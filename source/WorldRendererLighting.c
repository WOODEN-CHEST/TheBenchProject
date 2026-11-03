// The PBR shader and the frame lighting every lit shader (models, a CRT's glass, sprites, terrain, the rain's) gets
// through common/lighting (the sky's light: the sun and the moon through the air and the sky's own light as the ambient;
// the haze, shadows, per-object forward lights (points and spots, on or off, flickering) and a screen's light, and each draw's place: its haze and its
// ambient zones; the eye's night vision; the rain's: WorldRendererRain.c), and the PBR's per-material factors. The eye's adaptation is WorldRendererExposure.c's; interior lighting's parts are
// WorldRendererInterior.c's and WorldRendererSkyVisibility.c's.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "WorldRendererInternal.h"
#include "WorldLightCulling.h"
#include "WorldLightFlicker.h"
#include "Logger.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Macros.
/** Asset name of the world PBR (metallic/roughness) shader used to shade model objects. */
#define PBR_SHADER_ASSET_NAME ((const unsigned char*)u8"world_pbr")
/** Asset name of its skinned twin: the same fragment stage over a vertex stage that skins. */
#define PBR_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_pbr_skinned")
/** Asset name of its instanced twin: the same fragment stage over a vertex stage that places every copy of a cell
 *  (WorldRendererInstances.c draws with it). */
#define PBR_INSTANCED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_pbr_instanced")
/** The exposure the sun's disc is drawn for whatever the eye's: the disc's radiance is divided by the exposure and
 *  multiplied by this, so it keeps its look on screen through dusk, when the exposure rises far above the noon's (it
 *  would turn the setting disc white): about a clear noon's exposure, the one the disc's look is tuned at. */
#define SUN_DISC_SCREEN_EXPOSURE 1.08f
/** The most the sun's disc's intensity holds: its radiance, WORLD_ATMOSPHERE_SUN_DISC_RADIANCE times it, stays
 *  within the scene's half floats whatever sun_intensity is (an infinity there times the moon's cover or a sky_tint of
 *  0 would be a NaN). */
#define SUN_DISC_MAX_INTENSITY (WORLD_SKY_LIGHT_MAX_LIGHT / WORLD_ATMOSPHERE_SUN_DISC_RADIANCE)
/** Base world-space depth bias for the point-light shadow comparison; the PBR shader SLOPE-SCALES this (much
 *  larger where the light grazes a surface, a small fraction head-on). Raise if acne (streaky self-shadowing),
 *  lower if peter-panning (the shadow detaches from its caster). */
#define POINT_SHADOW_BIAS 0.35f
/** Room for the name of one forward-light array element ("forwardLightPositions[N]") when probing the shader. */
#define FORWARD_LIGHT_UNIFORM_NAME_CAPACITY 48
/** A shadow-casting light's text in the lights' status line: its object's id and its whole name. */
#define LIGHT_STATUS_TEXT_CAPACITY (WORLD_OBJECT_NAME_MAX_LENGTH + 48U)


// Static functions.
/* Relative luminance of a linear RGB colour (Rec. 709 weights). */
static float Luminance(const float rgb[3])
{
    return 0.2126f*rgb[0] + 0.7152f*rgb[1] + 0.0722f*rgb[2];
}

/* Points a sampler uniform at a fixed texture unit. Sampler values persist in the program object, so this is
 * done once at load (raylib's SetShaderValue binds the program and ignores location -1). */
static void AssignSamplerUnit(Shader shader, int location, WorldRendererTextureUnit unit)
{
    int UnitIndex = (int)unit;
    SetShaderValue(shader, location, &UnitIndex, SHADER_UNIFORM_INT);
}

/* How many entries the shader's forward-light arrays have, up to WORLD_MAX_FORWARD_LIGHTS: the highest N whose
 * element forwardLightPositions[N-1] has a location (the loop indexes the arrays dynamically, so every element of
 * the declared size is active). 0 when the arrays are absent. */
static int CountShaderForwardLights(Shader shader)
{
    for (int Count = WORLD_MAX_FORWARD_LIGHTS; Count > 0; Count--)
    {
        char Name[FORWARD_LIGHT_UNIFORM_NAME_CAPACITY];
        snprintf(Name, sizeof(Name), "forwardLightPositions[%d]", Count - 1);
        if (GetShaderLocation(shader, Name) >= 0)
        {
            return Count;
        }
    }
    return 0;
}

/* Caches every frame-lighting uniform location of a shader that includes common/lighting. */
static void CacheLitLocations(WorldRendererLitLocations* lit, Shader shader)
{
    lit->_locSunDirection = GetShaderLocation(shader, "sunDirection");
    lit->_locMoonDirection = GetShaderLocation(shader, "moonDirection");
    lit->_locSkyLightPerHeight = GetShaderLocation(shader, "skyLightPerHeight");
    lit->_locSunLight = GetShaderLocation(shader, "sunLight");
    lit->_locMoonLight = GetShaderLocation(shader, "moonLight");
    lit->_locSkyIrradiance = GetShaderLocation(shader, "skyIrradiance");
    lit->_locGroundIrradiance = GetShaderLocation(shader, "groundIrradiance");
    lit->_locIsMoonLit = GetShaderLocation(shader, "isMoonLit");
    lit->_locAmbientTint = GetShaderLocation(shader, "ambientTint");
    lit->_locNightLight = GetShaderLocation(shader, "nightLight");
    lit->_locSurfaceWetness = GetShaderLocation(shader, "surfaceWetness");

    lit->_locAmbientOcclusionMap = GetShaderLocation(shader, "ambientOcclusionMap");
    lit->_locAoMapActive = GetShaderLocation(shader, "aoMapActive");
    lit->_locAoResolution = GetShaderLocation(shader, "aoResolution");
    lit->_locSkyVisibilityOnScreen = GetShaderLocation(shader, "skyVisibilityOnScreen");
    lit->_locDrawInPrepass = GetShaderLocation(shader, "drawInPrepass");

    lit->_locShadowLevelCount = GetShaderLocation(shader, "shadowLevelCount");
    lit->_locShadowLevelMatrices = GetShaderLocation(shader, "shadowLevelMatrices");
    lit->_locShadowLevelOffsets = GetShaderLocation(shader, "shadowLevelOffsets");
    lit->_locShadowLevelEdge = GetShaderLocation(shader, "shadowLevelEdge");
    lit->_locShadowLevelBandStart = GetShaderLocation(shader, "shadowLevelBandStart");
    lit->_locShadowLevelLogFactor = GetShaderLocation(shader, "shadowLevelLogFactor");
    lit->_locShadowReachFade = GetShaderLocation(shader, "shadowReachFade");
    lit->_locShadowLevels = GetShaderLocation(shader, "shadowLevels");
    lit->_locShadowLevelView = GetShaderLocation(shader, "shadowLevelView");
    lit->_locShadowStrength = GetShaderLocation(shader, "shadowStrength");
    lit->_locShadowLightDirection = GetShaderLocation(shader, "shadowLightDirection");
    lit->_locSunShadowFade = GetShaderLocation(shader, "sunShadowFade");
    lit->_locMoonShadowFade = GetShaderLocation(shader, "moonShadowFade");

    lit->_locPointShadowActive = GetShaderLocation(shader, "pointShadowActive");
    lit->_locPointShadowLightPos = GetShaderLocation(shader, "pointShadowLightPos");
    lit->_locPointShadowLightRadiance = GetShaderLocation(shader, "pointShadowLightRadiance");
    lit->_locPointShadowLightRange = GetShaderLocation(shader, "pointShadowLightRange");
    lit->_locPointShadowCube = GetShaderLocation(shader, "pointShadowCube");
    lit->_locPointShadowFar = GetShaderLocation(shader, "pointShadowFar");
    lit->_locPointShadowBias = GetShaderLocation(shader, "pointShadowBias");

    lit->_locSpotShadowMatrices = GetShaderLocation(shader, "spotShadowMatrices");
    lit->_locSpotShadowLookups = GetShaderLocation(shader, "spotShadowLookups");
    lit->_locSpotShadowTexel = GetShaderLocation(shader, "spotShadowTexel");
    lit->_locSpotShadowMaps = GetShaderLocation(shader, "spotShadowMaps");

    lit->_locForwardLightCount = GetShaderLocation(shader, "forwardLightCount");
    lit->_locForwardLightPositions = GetShaderLocation(shader, "forwardLightPositions");
    lit->_locForwardLightRadiances = GetShaderLocation(shader, "forwardLightRadiances");
    lit->_locForwardLightRanges = GetShaderLocation(shader, "forwardLightRanges");
    lit->_locForwardLightDirections = GetShaderLocation(shader, "forwardLightDirections");
    lit->_locForwardLightCones = GetShaderLocation(shader, "forwardLightCones");
    lit->_locForwardLightShadows = GetShaderLocation(shader, "forwardLightShadows");

    lit->_locHazeViewProjection = GetShaderLocation(shader, "hazeViewProjection");
    lit->_locHazeMultiplier = GetShaderLocation(shader, "hazeMultiplier");
    lit->_locReflectionsDeferred = GetShaderLocation(shader, "reflectionsDeferred");
    lit->_locHasReflections = GetShaderLocation(shader, "hasReflections");

    lit->_locNightVisionFieldLuminance = GetShaderLocation(shader, "nightVisionFieldLuminance");
    lit->_locNightVisionHalfLuminance = GetShaderLocation(shader, "nightVisionHalfLuminance");
    lit->_locNightVisionSteepness = GetShaderLocation(shader, "nightVisionSteepness");
    lit->_locNightVisionStrength = GetShaderLocation(shader, "nightVisionStrength");
    lit->_locNightVisionTint = GetShaderLocation(shader, "nightVisionTint");
    WorldRendererCommon_PrepareCurvature(shader, &lit->_curvature);
}

/* Caches a PBR program's per-material scalar-factor and map-presence locations, uploaded per material by the mesh draw
 * loop. */
static void CacheMaterialLocations(WorldRendererPbrProgram* program, Shader shader)
{
    program->_locMetallic = GetShaderLocation(shader, "metallic");
    program->_locRoughness = GetShaderLocation(shader, "roughness");
    program->_locAo = GetShaderLocation(shader, "ao");
    program->_locEmissiveColor = GetShaderLocation(shader, "emissiveColor");
    program->_locEmissiveIntensity = GetShaderLocation(shader, "emissiveIntensity");
    program->_locHasMraMap = GetShaderLocation(shader, "hasMraMap");
    program->_locHasNormalMap = GetShaderLocation(shader, "hasNormalMap");
    program->_locHasEmissiveMap = GetShaderLocation(shader, "hasEmissiveMap");
    program->_locMaterialPorosity = GetShaderLocation(shader, "materialPorosity");
    program->_locMaterialPuddles = GetShaderLocation(shader, "materialPuddles");
    program->_locIsAlbedoMapBottomUp = GetShaderLocation(shader, "isAlbedoMapBottomUp");
    program->_locIsEmissiveMapBottomUp = GetShaderLocation(shader, "isEmissiveMapBottomUp");
}

/* The shadow light from the luminance each light gives a surface facing it, into the frame. */
static void ChooseShadowLight(WorldRenderFrame* frame, float sunLuminance, float moonLuminance)
{
    WorldShadowLightChoice Choice = WorldEnvironment_ChooseShadowLight(frame->SunDirection, sunLuminance,
        frame->MoonDirection, moonLuminance);
    frame->ShadowLight = Choice.Light;
    frame->ShadowDirection = Choice.Direction;
    frame->ShadowFade = Choice.Fade;
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererLighting_LoadPbrProgram(WorldRenderer* self, WorldRendererPbrProgram* program,
    const unsigned char* assetName)
{
    program->_shader = WorldRendererCommon_LoadShader(self, assetName);
    program->_palette = (WorldRendererPalette){ ._location = -1 };
    if (program->_shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(program->_shader);
    WorldRendererLighting_PrepareLitShader(self, RayShader, assetName, &program->_lit);
    CacheMaterialLocations(program, RayShader);
    WorldRendererCommon_PrepareDither(RayShader, &program->_dither);
    WorldRendererCommon_PreparePalette(RayShader, &program->_palette);

    // The renderer's own location table: the asset's, with the material map samplers pointed at the PBR
    // shader's sampler names, so DrawMesh binds each material's textures to their conventional units (albedo
    // -> 0 is already raylib's default "texture0"). The METALNESS slot carries the packed ORM map, matching where
    // ModelDefinition stores the mra texture. Patching a copy leaves the shared shader asset's table untouched.
    Memory_Copy(RayShader.locs, program->_locs, sizeof(program->_locs));
    program->_locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(RayShader, "mraMap");
    program->_locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(RayShader, "normalMap");
    program->_locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(RayShader, "emissiveMap");
    program->_passShader = (Shader){ .id = RayShader.id, .locs = program->_locs };
}

void WorldRendererLighting_LoadShader(WorldRenderer* self)
{
    WorldRendererLighting_LoadPbrProgram(self, &self->_pbr._rigid, PBR_SHADER_ASSET_NAME);
    if (self->_pbr._rigid._shader == NULL)
    {
        return; // skinned meshes need the lit pass too
    }
    WorldRendererLighting_LoadPbrProgram(self, &self->_pbr._skinned, PBR_SKINNED_SHADER_ASSET_NAME);
    if ((self->_pbr._skinned._shader != NULL) && (self->_pbr._skinned._palette._location < 0))
    {
        WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: \"world_pbr_skinned\" has no bone "
            u8"palette (bonePalette); skinned models draw in their bind pose.");
    }
    WorldRendererLighting_LoadPbrProgram(self, &self->_pbr._instanced, PBR_INSTANCED_SHADER_ASSET_NAME);
}

void WorldRendererLighting_PrepareLitShader(WorldRenderer* self, Shader shader, const unsigned char* assetName,
    WorldRendererLitLocations* outLocations)
{
    CacheLitLocations(outLocations, shader);

    // Point the renderer-owned samplers at their fixed units once (see WorldRendererTextureUnit).
    AssignSamplerUnit(shader, outLocations->_locShadowLevels, WorldRendererTextureUnit_ShadowLevels);
    AssignSamplerUnit(shader, outLocations->_locPointShadowCube, WorldRendererTextureUnit_PointShadowCube);
    AssignSamplerUnit(shader, outLocations->_locSpotShadowMaps, WorldRendererTextureUnit_SpotShadows);
    AssignSamplerUnit(shader, outLocations->_locAmbientOcclusionMap, WorldRendererTextureUnit_AmbientOcclusion);
    // The air and the haze (the volume's sampler on its own unit), and interior lighting (the sky's visibility map on
    // its own).
    WorldRendererAtmosphere_PrepareShader(shader, &outLocations->_atmosphere);
    WorldRendererAtmosphere_PrepareHazeShader(shader, true, &outLocations->_haze);
    WorldRendererInterior_PrepareShader(shader, &outLocations->_interior);
    WorldRendererClouds_PrepareLitShader(shader, &outLocations->_cloudShadow);
    // The rain (its map, the puddle mask), and the sky view the puddles mirror on a unit of its own: the lit programs
    // read the haze's volume where the sky pass has the table.
    WorldRendererRain_PrepareLitShader(shader, &outLocations->_rain);
    AssignSamplerUnit(shader, outLocations->_atmosphere._locSkyViewTable, WorldRendererTextureUnit_RainSkyView);
    // A screen's light, its picture on a unit of its own; the terrain's far shadows, their maps on theirs.
    WorldRendererSurfaces_PrepareLitShader(shader, &outLocations->_screenLight);
    WorldRendererFarShadows_PrepareLitShader(shader, &outLocations->_farShadow);

    // The shader's forward-light arrays must hold the WORLD_MAX_FORWARD_LIGHTS the culling may select; with fewer,
    // upload only as many as fit (the shader also clamps its loop to its own array size).
    outLocations->_maxForwardLights = CountShaderForwardLights(shader);
    // A program that reads no forward lights at all (the lightning bolt's) has none to hold.
    if ((outLocations->_maxForwardLights > 0) && (outLocations->_maxForwardLights < WORLD_MAX_FORWARD_LIGHTS)
        && (self->_logger != NULL))
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"WorldRenderer: \"%s\" holds %d forward lights, fewer than the %d the renderer "
            u8"selects (WORLD_MAX_FORWARD_LIGHTS); objects get at most %d.",
            (const char*)assetName, outLocations->_maxForwardLights, WORLD_MAX_FORWARD_LIGHTS,
            outLocations->_maxForwardLights);
        Error_Deconstruct(&LogResult);
    }
}

void WorldRendererLighting_PrepareSkyLight(WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    WorldRendererCommon_ColorToLinear(Environment->MoonColor, frame->MoonColor);

    // The C mirror's tables follow the air.
    WorldRendererAtmosphereState* Atmosphere = &self->_atmosphere;
    WorldRendererAtmosphere_UpdateSkyLightTables(self, &frame->AtmosphereMedium);

    // One moonlight for the surfaces and the air: the moonlit sky's (its gates save the moon's work where it cannot be
    // seen: with the sun up, with the moon far below the horizon).
    WorldSkyLights Lights =
    {
        .SunDirection = frame->SunDirection, .Daylight = frame->Daylight, .SunIntensity = frame->SunIntensity,
        .MoonDirection = frame->MoonDirection, .MoonPhase = frame->MoonPhase, .MoonIllumination = frame->MoonIllumination,
        .MoonIntensity = frame->MoonSkyIntensity
    };
    // The weather's clouds leave a share of the sun's and the moon's light above the air: what they light below them (the
    // surfaces, the air, the far ground) dims alike, and the clouds' deck sends down the light falling on its top (the
    // clear sky's at the clouds' base: a whiter sun, less sky above) as diffuse light. Drawn clouds are lit by that light
    // too.
    WorldWeatherLight* Weather = &frame->Weather;
    bool HasDeck = WorldWeatherLight_HasClouds(Weather);
    WorldSkyLightFrame CloudTop;
    Memory_Zero(&CloudTop, sizeof(CloudTop));
    if (HasDeck || frame->AreCloudsActive)
    {
        WorldSkyLight_GetFrameLight(Atmosphere->_skyLight.Tables, &frame->AtmosphereMedium, Environment, &Lights,
            frame->LightPollutionRadiance, (double)frame->Clouds.Low.Base, &CloudTop);
    }
    if (HasDeck)
    {
        Lights.SunIntensity *= Weather->DirectShare;
        Lights.MoonIntensity *= Weather->DirectShare;
        WorldSkyLight_GetFrameLight(Atmosphere->_skyLight.Tables, &frame->AtmosphereMedium, Environment, &Lights,
            frame->LightPollutionRadiance, (double)frame->AtmosphereViewHeight, &frame->SkyLight);
        WorldWeatherLight_AddDeck(Weather, &CloudTop, frame->SunDirection, frame->MoonDirection,
            frame->AtmosphereMedium.GroundAlbedo, &frame->SkyLight);
    }
    else
    {
        WorldSkyLight_GetFrameLight(Atmosphere->_skyLight.Tables, &frame->AtmosphereMedium, Environment, &Lights,
            frame->LightPollutionRadiance, (double)frame->AtmosphereViewHeight, &frame->SkyLight);
        // No deck: only a flash's light in the air is set (the light stays the clear sky's, bit for bit).
        WorldSkyLightFrame Clear = frame->SkyLight;
        WorldWeatherLight_AddDeck(Weather, &Clear, frame->SunDirection, frame->MoonDirection,
            frame->AtmosphereMedium.GroundAlbedo, &frame->SkyLight);
    }
    const WorldSkyLightFrame* Light = &frame->SkyLight;
    frame->BaseSceneLuminance = WorldSkyLight_GetSceneLuminance(Light);
    // The light the eye is in: the sky's at the camera as its openness and the zones at it leave it, plus the zones' own
    // (the sky's whole light in the open and while interior lighting is off).
    WorldInterior_GetCameraLuminance(Light, &frame->CameraInterior, &frame->CameraSceneLuminance,
        &frame->CameraDayLuminance);
    // The sun's disc for the eye's exposure (the one it is at: this render's is set after the scene is drawn).
    float Exposure = WorldRendererExposure_GetCurrent(self, frame);
    frame->LastExposure = Exposure;
    float SunColor[3];
    WorldRendererCommon_ColorToLinear(Environment->SunColor, SunColor);
    // The light the air is lit by: the lights above the air, dimmed everywhere by the clouds' share while they are one even
    // deck; in full while the clouds are drawn, the air below their base taking their share (CloudLightShare) and the
    // clouds hiding the discs, the stars and the Milky Way per pixel instead of by their share (SkyObjectShare).
    const float* SunAboveAir = frame->AreCloudsActive ? CloudTop.SunAboveAir : Light->SunAboveAir;
    const float* MoonAboveAir = frame->AreCloudsActive ? CloudTop.MoonAboveAir : Light->MoonAboveAir;
    frame->CloudLightShare = frame->AreCloudsActive ? Weather->DirectShare : 1.0f;
    frame->SkyObjectShare = frame->AreCloudsActive ? 1.0f : Weather->DirectShare;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        frame->AirSunIntensity[Channel] = SunAboveAir[Channel] / (float)WORLD_ATMOSPHERE_SUN_LIGHT;
        frame->AirMoonIntensity[Channel] = MoonAboveAir[Channel] / (float)WORLD_ATMOSPHERE_SUN_LIGHT;
        // fminf takes an absurd intensity's infinity to the most the disc holds; the clouds hide their share of it.
        frame->SunDiscIntensity[Channel] = fminf(SunColor[Channel] * fmaxf(frame->SunIntensity, 0.0f)
            * SUN_DISC_SCREEN_EXPOSURE / Exposure, (float)SUN_DISC_MAX_INTENSITY) * frame->SkyObjectShare;
    }
    // The light that lights the drawn clouds, and their shadow map.
    if (frame->AreCloudsActive)
    {
        WorldRendererClouds_PrepareLight(self, frame, &CloudTop);
    }
    // The lit shaders take it by height from the volume's last row while the atmosphere pass writes it there.
    frame->IsSkyLightPerHeight = frame->AreAtmosphereTablesActive && WorldRendererAtmosphere_IsVolumeAvailable(self);
    frame->IsMoonLit = frame->MoonSkyIntensity > 0.0f;
    // Night vision reads the daylight on the real scale and the night's lights (raised for play) through its power law:
    // the light the eye is in, a room's own light counted as daylight (it is lamp light, real light).
    frame->NightVisionField = NightVision_GetFieldLuminance(frame->CameraDayLuminance,
        frame->CameraSceneLuminance - frame->CameraDayLuminance);
    frame->NightVision = self->_nightVision;
    // The shadow light: the brighter of what the sun and the moon give the camera's surroundings.
    ChooseShadowLight(frame, Luminance(Light->Sun), Luminance(Light->Moon));
}

void WorldRendererLighting_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;

    // shadowStrength is shared by the shadow levels' and the point shadow's visibility, so it is gated on global shadow
    // enablement (NOT the sun being up) — otherwise a point light would cast no shadow at night. Which directional
    // light the levels darken, and how much, is the shader's sunShadowFade / moonShadowFade (0 for both while the
    // levels do not run, so a stale map is never sampled).
    float EffectiveShadowStrength = Environment->ShadowStrength * WorldRendererCommon_ConfigShadowStrength(self);
    frame->ShadowStrength = (Environment->AreShadowsEnabled && (EffectiveShadowStrength > 0.0f))
        ? EffectiveShadowStrength : 0.0f;

    // The cube shadow's light is shaded on its own, with its flicker; every other light that is on is a candidate, the
    // shadowed spots with their maps' layers.
    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    size_t Excluded[1];
    size_t ExcludedCount = 0;
    if (frame->IsPointShadowActive && (frame->ShadowLightIndex < LightCount))
    {
        WorldRendererLighting_GetLightRadiance(&Lights[frame->ShadowLightIndex], frame->LightSeconds,
            frame->PointShadowRadiance);
        Excluded[ExcludedCount++] = frame->ShadowLightIndex;
    }
    size_t ShadowedSpots[WORLD_RENDERER_MAX_SPOT_SHADOWS];
    for (uint32_t Spot = 0U; Spot < frame->SpotShadowCount; Spot++)
    {
        ShadowedSpots[Spot] = frame->SpotShadows[Spot].LightIndex;
    }
    GenericBuffer_Clear(&self->_pbr._forwardLights);
    GenericBuffer_Clear(&self->_pbr._forwardShading);
    WorldRendererLighting_BuildForwardLights(Lights, LightCount, Excluded, ExcludedCount, ShadowedSpots,
        frame->SpotShadowCount, frame->LightSeconds, &self->_pbr._forwardLights, &self->_pbr._forwardShading);
}

void WorldRendererLighting_GetLightRadiance(const WorldRenderLight* light, double seconds, float outRgb[3])
{
    WorldRendererCommon_ColorToLinear(light->Color, outRgb);
    float Flicker = WorldLightFlicker_GetFactor(light->Flicker, light->FlickerAmount, light->FlickerSpeed, seconds,
        light->ObjectId);
    float Scale = light->IsOn ? (light->Intensity * Flicker) : 0.0f;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        outRgb[Channel] *= Scale;
    }
}

void WorldRendererLighting_GetConeFalloff(WorldLightType type, WorldLightCone cone, float out[2])
{
    if (type != WorldLightType_Spot)
    {
        out[0] = 0.0f;
        out[1] = 1.0f;
        return;
    }
    float CosInner = cosf(cone.InnerAngle);
    float CosOuter = cosf(cone.OuterAngle);
    // The band is at least WORLD_RENDERER_MIN_CONE_SPAN wide (a hard edge stays finite), but never more than a tenth of
    // the cone's own room between its outer angle and its axis, so a very narrow spot still reaches full light inside.
    float Room = 1.0f - CosOuter;
    float Span = fmaxf(CosInner - CosOuter, fminf(WORLD_RENDERER_MIN_CONE_SPAN, 0.1f * Room));
    float Scale = 1.0f / ((Span > 0.0f) ? Span : WORLD_RENDERER_MIN_CONE_SPAN);
    out[0] = Scale;
    out[1] = -CosOuter * Scale;
}

/* Whether @p index is among the @p count indices of @p excluded. */
static bool IsExcluded(const size_t* excluded, size_t count, size_t index)
{
    for (size_t Entry = 0; Entry < count; Entry++)
    {
        if (excluded[Entry] == index)
        {
            return true;
        }
    }
    return false;
}

/* The map layer of light @p index: its place among the @p count shadowed spots, or -1. */
static int GetShadowLayer(const size_t* shadowedSpots, size_t count, size_t index)
{
    for (size_t Spot = 0; Spot < count; Spot++)
    {
        if (shadowedSpots[Spot] == index)
        {
            return (int)Spot;
        }
    }
    return -1;
}

bool WorldRendererLighting_BuildForwardLights(const WorldRenderLight* lights, size_t lightCount, const size_t* excluded,
    size_t excludedCount, const size_t* shadowedSpots, size_t shadowedSpotCount, double seconds,
    GenericBuffer* outCandidates, GenericBuffer* outShading)
{
    for (size_t Index = 0; Index < lightCount; Index++)
    {
        const WorldRenderLight* Light = &lights[Index];
        if (!Light->IsOn || !(Light->Range > 0.0f) || !(Light->Intensity > 0.0f)
            || IsExcluded(excluded, excludedCount, Index))
        {
            continue;
        }
        float Flicker = WorldLightFlicker_GetFactor(Light->Flicker, Light->FlickerAmount, Light->FlickerSpeed, seconds,
            Light->ObjectId);
        if (!(Flicker > 0.0f))
        {
            continue; // dark this frame: it lights nothing, and its draws take the next strongest light
        }

        bool IsSpot = (Light->Type == WorldLightType_Spot);
        WorldCullingLight Candidate =
        {
            .Position = Light->RenderPosition,
            .Range = Light->Range,
            .Intensity = Light->Intensity * Flicker,
            .IsSpot = IsSpot,
            .Direction = Light->Direction,
            .CosOuter = cosf(Light->Cone.OuterAngle)
        };
        WorldRendererForwardLight Shading;
        WorldRendererLighting_GetLightRadiance(Light, seconds, Shading.Rgb);
        Shading.Direction[0] = IsSpot ? Light->Direction.x : 0.0f;
        Shading.Direction[1] = IsSpot ? Light->Direction.y : 0.0f;
        Shading.Direction[2] = IsSpot ? Light->Direction.z : 0.0f;
        WorldRendererLighting_GetConeFalloff(Light->Type, Light->Cone, Shading.Cone);
        Shading.ShadowLayer = GetShadowLayer(shadowedSpots, shadowedSpotCount, Index);
        if (!GenericBuffer_AddLast(outCandidates, &Candidate) || !GenericBuffer_AddLast(outShading, &Shading))
        {
            // Cannot happen short of address-space exhaustion; keep the two lists parallel by dropping both.
            GenericBuffer_Clear(outCandidates);
            GenericBuffer_Clear(outShading);
            return false;
        }
    }
    return true;
}

void WorldRendererLighting_PackForwardLights(const WorldCullingLight* candidates, const WorldRendererForwardLight* shading,
    const size_t* selected, size_t count, WorldRendererForwardLightArrays* outArrays)
{
    size_t Count = (count < (size_t)WORLD_MAX_FORWARD_LIGHTS) ? count : (size_t)WORLD_MAX_FORWARD_LIGHTS;
    outArrays->Count = (int)Count;
    for (size_t Index = 0; Index < Count; Index++)
    {
        const WorldCullingLight* Light = &candidates[selected[Index]];
        const WorldRendererForwardLight* Values = &shading[selected[Index]];
        outArrays->Positions[(Index * 3U) + 0U] = Light->Position.x;
        outArrays->Positions[(Index * 3U) + 1U] = Light->Position.y;
        outArrays->Positions[(Index * 3U) + 2U] = Light->Position.z;
        outArrays->Ranges[Index] = Light->Range;
        for (size_t Channel = 0; Channel < 3U; Channel++)
        {
            outArrays->Radiances[(Index * 3U) + Channel] = Values->Rgb[Channel];
            outArrays->Directions[(Index * 3U) + Channel] = Values->Direction[Channel];
        }
        outArrays->Cones[(Index * 2U) + 0U] = Values->Cone[0];
        outArrays->Cones[(Index * 2U) + 1U] = Values->Cone[1];
        outArrays->Shadows[Index] = Values->ShadowLayer;
    }
}

/* The cube-shadow light of the frame, or NULL while the cube shadow does not run. */
static const WorldRenderLight* GetPointShadowLight(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    return (frame->IsPointShadowActive && (frame->ShadowLightIndex < LightCount)) ? &Lights[frame->ShadowLightIndex]
        : NULL;
}

/* Uploads the shadow levels to one lit shader (common/lighting): how many are read (0 while they did not run), each one's
 * matrices as its map holds them and its lookups' offsets, where a level's lookups end and its band starts, the factor
 * between the levels, the reach's fade, the maps' texel and the debug view. */
static void UploadShadowLevels(const WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererLitLocations* lit)
{
    int Count = frame->AreShadowLevelsActive ? (int)frame->ShadowLevelCount : 0;
    SetShaderValue(shader, lit->_locShadowLevelCount, &Count, SHADER_UNIFORM_INT);
    int View = self->_shadowLevels._isLevelViewEnabled ? 1 : 0;
    SetShaderValue(shader, lit->_locShadowLevelView, &View, SHADER_UNIFORM_INT);
    if (Count == 0)
    {
        return;
    }
    const WorldShadowLevelsSettings* Settings = &frame->ShadowLevelSettings;
    Matrix Matrices[WORLD_RENDERER_MAX_SHADOW_LEVELS];
    float Offsets[WORLD_RENDERER_MAX_SHADOW_LEVELS * 2];
    for (int Level = 0; Level < Count; Level++)
    {
        Matrices[Level] = frame->ShadowLevels[Level].ViewProjection;
        Offsets[(Level * 2) + 0] = frame->ShadowLevels[Level].DepthBias;
        Offsets[(Level * 2) + 1] = frame->ShadowLevels[Level].NormalOffset;
    }
    if (lit->_locShadowLevelMatrices >= 0)
    {
        rlEnableShader(shader.id);
        rlSetUniformMatrices(lit->_locShadowLevelMatrices, Matrices, Count);
    }
    SetShaderValueV(shader, lit->_locShadowLevelOffsets, Offsets, SHADER_UNIFORM_VEC2, Count);
    float Edge = WorldShadowLevels_GetEdge(Settings);
    float BandStart = Edge * (1.0f - WORLD_SHADOW_BLEND_SHARE);
    float LogFactor = logf(Settings->Factor);
    float ReachFade[2] = { WorldShadowLevels_GetReachFadeStart(Settings), Settings->Reach };
    SetShaderValue(shader, lit->_locShadowLevelEdge, &Edge, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locShadowLevelBandStart, &BandStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locShadowLevelLogFactor, &LogFactor, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locShadowReachFade, ReachFade, SHADER_UNIFORM_VEC2);
}

/* Uploads the shadowed spots' maps to one lit shader (common/lighting): each one's matrix and lookups (the normal offset's
 * and the depth bias's scales), and the maps' texel. (The spots are forward lights; their layers come with a draw's
 * lights.) */
static void UploadSpotShadows(const WorldRenderFrame* frame, Shader shader, const WorldRendererLitLocations* lit)
{
    int Count = (int)frame->SpotShadowCount;
    if (Count == 0)
    {
        return;
    }
    float Lookups[WORLD_RENDERER_MAX_SPOT_SHADOWS * 2];
    Matrix Matrices[WORLD_RENDERER_MAX_SPOT_SHADOWS];
    for (int Spot = 0; Spot < Count; Spot++)
    {
        const WorldRendererSpotShadowFrame* Entry = &frame->SpotShadows[Spot];
        Lookups[(Spot * 2) + 0] = Entry->Map.NormalOffsetScale;
        Lookups[(Spot * 2) + 1] = Entry->Map.DepthBiasScale;
        Matrices[Spot] = Entry->Map.ViewProjection;
    }
    SetShaderValueV(shader, lit->_locSpotShadowLookups, Lookups, SHADER_UNIFORM_VEC2, Count);
    if (lit->_locSpotShadowMatrices >= 0)
    {
        rlEnableShader(shader.id);
        rlSetUniformMatrices(lit->_locSpotShadowMatrices, Matrices, Count);
    }
    float Texel = 1.0f / (float)WORLD_LOCAL_SHADOWS_SPOT_MAP_SIZE;
    SetShaderValue(shader, lit->_locSpotShadowTexel, &Texel, SHADER_UNIFORM_FLOAT);
}

/* Uploads the frame's lighting values to one lit shader (common/lighting): the sun and the moon, the ambient, the haze,
 * the night vision, the shadow levels' fades, light direction, matrices and lookups, the AO switch and the cube-shadow
 * light.
 * Positions and matrices are in render space, where the camera sits at the origin (so it needs no uniform). The
 * textures are bound once for every lit shader (BindFrameTextures). */
static void UploadLitUniforms(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererLitLocations* lit, bool applyScreenAo, bool areReflectionsDeferred)
{
    // The two directional lights' directions (they light the ground haze too), and the sky's light: by height from the
    // volume's last row, or the camera's from these uniforms (the C mirror's).
    float SunDir[3] = { frame->SunDirection.x, frame->SunDirection.y, frame->SunDirection.z };
    SetShaderValue(shader, lit->_locSunDirection, SunDir, SHADER_UNIFORM_VEC3);
    float MoonDir[3] = { frame->MoonDirection.x, frame->MoonDirection.y, frame->MoonDirection.z };
    SetShaderValue(shader, lit->_locMoonDirection, MoonDir, SHADER_UNIFORM_VEC3);
    const WorldSkyLightFrame* Light = &frame->SkyLight;
    int PerHeight = frame->IsSkyLightPerHeight ? 1 : 0;
    SetShaderValue(shader, lit->_locSkyLightPerHeight, &PerHeight, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lit->_locSunLight, Light->Sun, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, lit->_locMoonLight, Light->Moon, SHADER_UNIFORM_VEC3);
    // A lightning flash lights surfaces with the sky, but the eye's anchor leaves it out (the C mirror's frame light).
    float SkyIrradiance[3];
    float GroundIrradiance[3];
    const WorldWeatherLight* Weather = &frame->Weather;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        SkyIrradiance[Channel] = Light->Sky[Channel] + Weather->FlashIrradiance[Channel];
        GroundIrradiance[Channel] = Light->Ground[Channel]
            + frame->AtmosphereMedium.GroundAlbedo[Channel] * Weather->FlashIrradiance[Channel];
    }
    SetShaderValue(shader, lit->_locSkyIrradiance, SkyIrradiance, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, lit->_locGroundIrradiance, GroundIrradiance, SHADER_UNIFORM_VEC3);
    int MoonLit = frame->IsMoonLit ? 1 : 0;
    SetShaderValue(shader, lit->_locIsMoonLit, &MoonLit, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lit->_locAmbientTint, Light->AmbientTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, lit->_locNightLight, Light->Night, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, lit->_locSurfaceWetness, &Weather->Wetness, SHADER_UNIFORM_FLOAT);

    // The haze in front of every surface: the volume the atmosphere pass built for the scene camera's screen
    // directions (read through the scene's view-projection, also by the crisp pass), the frame's haze and the ground
    // haze. Each draw sets its own multiplier and zones; 1 and none until the first does.
    WorldRendererAtmosphere_UploadFrame(shader, &lit->_atmosphere, frame);
    WorldRendererAtmosphere_UploadHaze(shader, &lit->_haze, frame);
    SetShaderValueMatrix(shader, lit->_locHazeViewProjection, frame->ViewProjection);
    float NoHazeChange = 1.0f;
    SetShaderValue(shader, lit->_locHazeMultiplier, &NoHazeChange, SHADER_UNIFORM_FLOAT);
    // The reflections: the pass's surfaces hand theirs to the reflection pass, or add the sky's themselves. Each draw
    // sets whether it mirrors its surroundings; none until the first does.
    int Deferred = areReflectionsDeferred ? 1 : 0;
    SetShaderValue(shader, lit->_locReflectionsDeferred, &Deferred, SHADER_UNIFORM_INT);
    int NoReflections = 0;
    SetShaderValue(shader, lit->_locHasReflections, &NoReflections, SHADER_UNIFORM_INT);
    // Interior lighting: the sky's visibility map and the debug view.
    WorldRendererInterior_UploadFrame(self, frame, shader, &lit->_interior);
    // The clouds' shadow on the sun (or the moon) at every surface.
    WorldRendererClouds_UploadLitFrame(shader, &lit->_cloudShadow, frame);
    // The rain: what keeps it out, the puddles, the sky mirrored in water, a bolt's light.
    WorldRendererRain_UploadLitFrame(self, frame, shader, &lit->_rain);

    // The eye's night vision for the reflected light and the haze (strength 0 leaves every colour as it is), calibrated on
    // the frame's light scale.
    SetShaderValue(shader, lit->_locNightVisionFieldLuminance, &frame->NightVisionFieldLuminance, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locNightVisionHalfLuminance, &frame->NightVision.HalfLuminance, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locNightVisionSteepness, &frame->NightVision.Steepness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locNightVisionStrength, &frame->NightVisionStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locNightVisionTint, frame->NightVisionTint, SHADER_UNIFORM_VEC3);

    // The levels and the far shadows darken the light they were made for (the sun or the moon), by the frame's shadow
    // fade; with both fades 0 (neither ran) the shader samples no map at all.
    float SunShadowFade = 0.0f;
    float MoonShadowFade = 0.0f;
    if (frame->AreShadowLevelsActive || frame->AreFarShadowsActive)
    {
        SunShadowFade = (frame->ShadowLight == WorldShadowLight_Sun) ? frame->ShadowFade : 0.0f;
        MoonShadowFade = (frame->ShadowLight == WorldShadowLight_Moon) ? frame->ShadowFade : 0.0f;
    }
    SetShaderValue(shader, lit->_locSunShadowFade, &SunShadowFade, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locMoonShadowFade, &MoonShadowFade, SHADER_UNIFORM_FLOAT);
    float ShadowDir[3] = { frame->ShadowDirection.x, frame->ShadowDirection.y, frame->ShadowDirection.z };
    SetShaderValue(shader, lit->_locShadowLightDirection, ShadowDir, SHADER_UNIFORM_VEC3);
    UploadShadowLevels(self, frame, shader, lit);
    WorldRendererFarShadows_UploadLitFrame(shader, &lit->_farShadow, frame);

    // Screen-space AO: the crisp overlay passes applyScreenAo=false (its full-res gl_FragCoord would mis-sample this
    // low-res map). aoMapActive false => the shader leaves the ambient term unoccluded.
    int AoMapActive = (applyScreenAo && frame->IsAmbientPrepassActive) ? 1 : 0;
    int SkyOnScreen = (applyScreenAo && frame->IsAmbientPrepassActive && frame->IsSkyVisibilityOnScreen) ? 1 : 0;
    SetShaderValue(shader, lit->_locSkyVisibilityOnScreen, &SkyOnScreen, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lit->_locAoMapActive, &AoMapActive, SHADER_UNIFORM_INT);
    if (AoMapActive)
    {
        float AoResolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
        SetShaderValue(shader, lit->_locAoResolution, AoResolution, SHADER_UNIFORM_VEC2);
    }

    // Point-light (cube) shadow: the selected shadow light is shaded separately with its cube map. The shader gates
    // its own sampling on pointShadowActive.
    const WorldRenderLight* PointShadowLight = GetPointShadowLight(self, frame);
    int PointShadowActive = (PointShadowLight != NULL) ? 1 : 0;
    SetShaderValue(shader, lit->_locPointShadowActive, &PointShadowActive, SHADER_UNIFORM_INT);
    if (PointShadowLight != NULL)
    {
        float LightPos[3] =
        {
            PointShadowLight->RenderPosition.x, PointShadowLight->RenderPosition.y, PointShadowLight->RenderPosition.z
        };
        SetShaderValue(shader, lit->_locPointShadowLightPos, LightPos, SHADER_UNIFORM_VEC3);

        SetShaderValue(shader, lit->_locPointShadowLightRadiance, frame->PointShadowRadiance, SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, lit->_locPointShadowLightRange, &PointShadowLight->Range, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, lit->_locPointShadowFar, &frame->PointShadowFar, SHADER_UNIFORM_FLOAT);
        float PointBias = POINT_SHADOW_BIAS;
        SetShaderValue(shader, lit->_locPointShadowBias, &PointBias, SHADER_UNIFORM_FLOAT);
    }

    UploadSpotShadows(frame, shader, lit);

    SetShaderValue(shader, lit->_locShadowStrength, &frame->ShadowStrength, SHADER_UNIFORM_FLOAT);
    // The planet's curve in the vertex stage: every lit program draws a camera pass.
    WorldRendererCommon_UploadCurvature(shader, &lit->_curvature, frame);
}

/* Binds the frame's shadow maps, AO map, shadow cube and the haze's volume to their fixed units (the lit shaders'
 * samplers point there), once for every lit shader: texture units are context state. Bound manually, which works
 * because the lit passes draw with DrawMesh (a direct draw, not the 2D batch). */
static void BindFrameTextures(WorldRenderer* self, const WorldRenderFrame* frame, bool applyScreenAo)
{
    WorldRendererAtmosphere_BindVolume(self, frame);
    WorldRendererSkyVisibility_BindMap(self, frame);
    WorldRendererShadows_BindLevels(self, frame);
    WorldRendererShadows_BindSpots(self, frame);
    WorldRendererFarShadows_Bind(self, frame);
    WorldRendererClouds_BindShadow(self, frame);
    WorldRendererRain_BindFrameTextures(self, frame);
    if (applyScreenAo && frame->IsAmbientPrepassActive)
    {
        rlActiveTextureSlot(WorldRendererTextureUnit_AmbientOcclusion);
        rlEnableTexture(self->_ao._target.texture.id);
    }
    if (GetPointShadowLight(self, frame) != NULL)
    {
        rlActiveTextureSlot(WorldRendererTextureUnit_PointShadowCube);
        rlEnableTextureCubemap(self->_pointShadow._cube);
    }
}

/* Uploads the frame's lighting and the pass's dither pixel scale to a PBR program, when it loaded. */
static void UploadPbrFrame(WorldRenderer* self, const WorldRenderFrame* frame, const WorldRendererPbrProgram* program,
    bool applyScreenAo, bool areReflectionsDeferred, Vector2 ditherPixelScale)
{
    if (program->_shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(program->_shader);
    UploadLitUniforms(self, frame, RayShader, &program->_lit, applyScreenAo, areReflectionsDeferred);
    WorldRendererCommon_SetDitherPixelScale(RayShader, &program->_dither, ditherPixelScale);
}

void WorldRendererLighting_UploadFrame(WorldRenderer* self, const WorldRenderFrame* frame, bool applyScreenAo,
    bool areReflectionsDeferred, Vector2 ditherPixelScale, float windowPixelScale)
{
    bool HasPbr = (self->_pbr._rigid._shader != NULL);
    bool HasSprites = (self->_sprites._shader != NULL);
    bool HasTerrain = (self->_terrain._shader != NULL);
    UploadPbrFrame(self, frame, &self->_pbr._rigid, applyScreenAo, areReflectionsDeferred, ditherPixelScale);
    UploadPbrFrame(self, frame, &self->_pbr._skinned, applyScreenAo, areReflectionsDeferred, ditherPixelScale);
    UploadPbrFrame(self, frame, &self->_pbr._instanced, applyScreenAo, areReflectionsDeferred, ditherPixelScale);
    UploadPbrFrame(self, frame, &self->_crt._rigid, applyScreenAo, areReflectionsDeferred, ditherPixelScale);
    UploadPbrFrame(self, frame, &self->_crt._skinned, applyScreenAo, areReflectionsDeferred, ditherPixelScale);
    UploadPbrFrame(self, frame, &self->_water._rigid._program, applyScreenAo, areReflectionsDeferred, ditherPixelScale);
    UploadPbrFrame(self, frame, &self->_water._instanced._program, applyScreenAo, areReflectionsDeferred,
        ditherPixelScale);
    WorldRendererCrt_UploadPass(self, frame, windowPixelScale);
    // The unlit fallback's skinned program curves as the lit ones do (the materials' own raylib shaders draw flat).
    if (self->_unlit._skinnedShader != NULL)
    {
        WorldRendererCommon_UploadCurvature(GameShader_GetRaylibShader(self->_unlit._skinnedShader),
            &self->_unlit._skinnedCurvature, frame);
    }
    if (HasSprites)
    {
        Shader SpriteShader = GameShader_GetRaylibShader(self->_sprites._shader);
        UploadLitUniforms(self, frame, SpriteShader, &self->_sprites._lit, applyScreenAo, areReflectionsDeferred);
        WorldRendererCommon_SetDitherPixelScale(SpriteShader, &self->_sprites._dither, ditherPixelScale);
    }
    if (HasTerrain)
    {
        Shader TerrainShader = GameShader_GetRaylibShader(self->_terrain._shader);
        UploadLitUniforms(self, frame, TerrainShader, &self->_terrain._lit, applyScreenAo, areReflectionsDeferred);
        WorldRendererCommon_SetDitherPixelScale(TerrainShader, &self->_terrain._dither, ditherPixelScale);
    }
    if (HasPbr || HasSprites || HasTerrain)
    {
        BindFrameTextures(self, frame, applyScreenAo);
    }
}

void WorldRendererLighting_UploadLitProgram(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererLitLocations* locations, bool applyScreenAo, bool areReflectionsDeferred)
{
    UploadLitUniforms(self, frame, shader, locations, applyScreenAo, areReflectionsDeferred);
}

/* Names an object in the log, once, for having more forward lights reach a draw of it than the draw takes (a scene that
 * needs more shows up: clustered lighting would light it by all of them); past WORLD_RENDERER_LIGHT_OVERFLOW_LOG_LIMIT
 * objects one line says so and no more are named. */
static void LogLightOverflow(WorldRenderer* self, uint64_t objectId, size_t reaching, size_t taken)
{
    WorldRendererPbrState* State = &self->_pbr;
    const void* Data = State->_lightOverflowObjects._data;
    const uint64_t* Logged = Data;
    for (size_t Index = 0; Index < State->_lightOverflowObjects._count; Index++)
    {
        if (Logged[Index] == objectId)
        {
            return;
        }
    }
    if (State->_isLightOverflowLogFull)
    {
        return;
    }
    if ((State->_lightOverflowObjects._count >= WORLD_RENDERER_LIGHT_OVERFLOW_LOG_LIMIT)
        || !GenericBuffer_AddLast(&State->_lightOverflowObjects, &objectId))
    {
        State->_isLightOverflowLogFull = true;
        if (self->_logger != NULL)
        {
            Error LogResult = Logger_LogWarningFormatted(self->_logger,
                (const unsigned char*)u8"WorldRenderer: more objects have more lights reaching a draw than it takes; "
                u8"no more of them are named.");
            Error_Deconstruct(&LogResult);
        }
        return;
    }
    if (self->_logger == NULL)
    {
        return;
    }
    const unsigned char* Name = WorldRenderCache_FindObjectName(&self->_cache, objectId);
    Error LogResult = Logger_LogWarningFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: %zu lights reach a draw of object %" PRIu64 u8" (\"%s\"), more than "
        u8"the %zu one draw takes; it is lit by the strongest %zu (logged once per object).", reaching, objectId,
        (Name != NULL) ? (const char*)Name : "", taken, taken);
    Error_Deconstruct(&LogResult);
}

/* The forward lights a draw's bounding sphere takes: the strongest few the frame's candidates reach it with, as indices
 * into them; returns how many. An object with more lights reaching the draw than it takes is logged once. */
static size_t SelectObjectLights(WorldRenderer* self, const WorldRendererLitLocations* locations, Vector3 center,
    float radius, uint64_t objectId, size_t* outSelected)
{
    const void* CandidateData = self->_pbr._forwardLights._data;
    size_t Capacity = (size_t)locations->_maxForwardLights;
    size_t Reaching = 0;
    size_t Count = WorldLightCulling_SelectForSphere((const WorldCullingLight*)CandidateData,
        self->_pbr._forwardLights._count, center, radius, outSelected, Capacity, &Reaching);
    if ((objectId != 0U) && (Capacity > 0U) && (Reaching > Capacity))
    {
        LogLightOverflow(self, objectId, Reaching, Capacity);
    }
    return Count;
}

/* Uploads the forward lights a draw takes (SelectObjectLights) for the draw that follows. */
static void UploadSelectedLights(const WorldRenderer* self, Shader shader, const WorldRendererLitLocations* locations,
    const size_t* selected, size_t count)
{
    const void* CandidateData = self->_pbr._forwardLights._data;
    const void* ShadingData = self->_pbr._forwardShading._data;
    WorldRendererForwardLightArrays Arrays;
    WorldRendererLighting_PackForwardLights(CandidateData, ShadingData, selected, count, &Arrays);

    SetShaderValue(shader, locations->_locForwardLightCount, &Arrays.Count, SHADER_UNIFORM_INT);
    if (Arrays.Count > 0)
    {
        SetShaderValueV(shader, locations->_locForwardLightPositions, Arrays.Positions, SHADER_UNIFORM_VEC3, Arrays.Count);
        SetShaderValueV(shader, locations->_locForwardLightRadiances, Arrays.Radiances, SHADER_UNIFORM_VEC3, Arrays.Count);
        SetShaderValueV(shader, locations->_locForwardLightRanges, Arrays.Ranges, SHADER_UNIFORM_FLOAT, Arrays.Count);
        SetShaderValueV(shader, locations->_locForwardLightDirections, Arrays.Directions, SHADER_UNIFORM_VEC3,
            Arrays.Count);
        SetShaderValueV(shader, locations->_locForwardLightCones, Arrays.Cones, SHADER_UNIFORM_VEC2, Arrays.Count);
        SetShaderValueV(shader, locations->_locForwardLightShadows, Arrays.Shadows, SHADER_UNIFORM_INT, Arrays.Count);
    }
}

void WorldRendererLighting_UploadObjectLights(WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, Vector3 center, float radius, uint64_t objectId)
{
    WorldRendererSurfaces_UploadScreenLight(self, shader, &locations->_screenLight, center, radius);
    if (locations->_locForwardLightCount < 0)
    {
        return;
    }
    size_t Selected[WORLD_MAX_FORWARD_LIGHTS];
    size_t Count = SelectObjectLights(self, locations, center, radius, objectId, Selected);
    UploadSelectedLights(self, shader, locations, Selected, Count);
}

void WorldRendererLighting_UploadRunLights(WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, WorldRendererLitRun* run, Vector3 center, float radius,
    uint64_t objectId)
{
    if (locations->_screenLight._locActive >= 0)
    {
        const WorldRendererScreenLight* Nearest = WorldRendererSurfaces_FindScreenLight(self, center, radius);
        if (!run->_hasScreenLight || (run->_screenLight != Nearest))
        {
            WorldRendererSurfaces_UploadFoundScreenLight(shader, &locations->_screenLight, Nearest);
            run->_hasScreenLight = true;
            run->_screenLight = Nearest;
        }
    }
    if (locations->_locForwardLightCount < 0)
    {
        return;
    }
    size_t Selected[WORLD_MAX_FORWARD_LIGHTS];
    size_t Count = SelectObjectLights(self, locations, center, radius, objectId, Selected);
    if (run->_hasLights && (run->_lightCount == Count)
        && ((Count == 0U) || (memcmp(run->_lights, Selected, Count * sizeof(size_t)) == 0)))
    {
        return;
    }
    UploadSelectedLights(self, shader, locations, Selected, Count);
    run->_hasLights = true;
    run->_lightCount = Count;
    if (Count > 0U)
    {
        Memory_Copy(Selected, run->_lights, Count * sizeof(size_t));
    }
}

void WorldRendererLighting_SetDrawPlace(const WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, Vector3 center, float radius, float haze, bool isInPrepass,
    bool hasReflections)
{
    SetShaderValue(shader, locations->_locHazeMultiplier, &haze, SHADER_UNIFORM_FLOAT);
    int InPrepass = isInPrepass ? 1 : 0;
    SetShaderValue(shader, locations->_locDrawInPrepass, &InPrepass, SHADER_UNIFORM_INT);
    int Mirrors = hasReflections ? 1 : 0;
    SetShaderValue(shader, locations->_locHasReflections, &Mirrors, SHADER_UNIFORM_INT);
    WorldRendererInterior_UploadZones(self, shader, &locations->_interior, center, radius);
}

void WorldRendererLighting_SetRunDrawPlace(const WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, WorldRendererLitRun* run, Vector3 center, float radius, float haze,
    bool isInPrepass, bool hasReflections)
{
    if (!run->_hasPlace || (memcmp(&run->_haze, &haze, sizeof(haze)) != 0))
    {
        SetShaderValue(shader, locations->_locHazeMultiplier, &haze, SHADER_UNIFORM_FLOAT);
        run->_haze = haze;
    }
    if (!run->_hasPlace || (run->_isInPrepass != isInPrepass))
    {
        int InPrepass = isInPrepass ? 1 : 0;
        SetShaderValue(shader, locations->_locDrawInPrepass, &InPrepass, SHADER_UNIFORM_INT);
        run->_isInPrepass = isInPrepass;
    }
    if (!run->_hasPlace || (run->_hasReflections != hasReflections))
    {
        int Mirrors = hasReflections ? 1 : 0;
        SetShaderValue(shader, locations->_locHasReflections, &Mirrors, SHADER_UNIFORM_INT);
        run->_hasReflections = hasReflections;
    }
    run->_hasPlace = true;
    // While interior lighting runs every draw takes the zones its sphere touches; otherwise none, uploaded once.
    if (self->_interior._isActive || !run->_hasNoZones)
    {
        WorldRendererInterior_UploadZones(self, shader, &locations->_interior, center, radius);
        run->_hasNoZones = !self->_interior._isActive;
    }
}

/* raylib's DrawMesh uploads colDiffuse and binds the material's textures (via the map-sampler locations of the
 * pass shader's table), but NOT the scalar factors, the has*Map guards or the wetness params, so this does that. A map flag is true
 * only when the slot actually holds a texture; the shader must not sample an unbound map sampler (it would read
 * texture unit 0 = albedo — the silent-fallback bug seen across the post passes). */
void WorldRendererLighting_UploadMaterial(const WorldRendererPbrProgram* program, const Material* material,
    WorldRendererMapOrientation orientation)
{
    Shader RayShader = GameShader_GetRaylibShader(program->_shader);
    float Metallic = material->maps[MATERIAL_MAP_METALNESS].value;
    float Roughness = material->maps[MATERIAL_MAP_ROUGHNESS].value;
    float Ao = material->maps[MATERIAL_MAP_OCCLUSION].value;
    float EmissiveIntensity = material->maps[MATERIAL_MAP_EMISSION].value;
    float EmissiveColor[3];
    WorldRendererCommon_ColorToLinear(material->maps[MATERIAL_MAP_EMISSION].color, EmissiveColor);

    float Porosity = material->params[GAME_MATERIAL_PARAM_POROSITY];
    float Puddles = material->params[GAME_MATERIAL_PARAM_PUDDLES];
    int HasMra = (material->maps[MATERIAL_MAP_METALNESS].texture.id > 0) ? 1 : 0;
    int HasNormal = (material->maps[MATERIAL_MAP_NORMAL].texture.id > 0) ? 1 : 0;
    int HasEmissive = (material->maps[MATERIAL_MAP_EMISSION].texture.id > 0) ? 1 : 0;
    int IsAlbedoBottomUp = orientation.IsAlbedoBottomUp ? 1 : 0;
    int IsEmissiveBottomUp = orientation.IsEmissiveBottomUp ? 1 : 0;

    SetShaderValue(RayShader, program->_locMetallic, &Metallic, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locRoughness, &Roughness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locAo, &Ao, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locEmissiveColor, EmissiveColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, program->_locEmissiveIntensity, &EmissiveIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locHasMraMap, &HasMra, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, program->_locHasNormalMap, &HasNormal, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, program->_locHasEmissiveMap, &HasEmissive, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, program->_locMaterialPorosity, &Porosity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locMaterialPuddles, &Puddles, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locIsAlbedoMapBottomUp, &IsAlbedoBottomUp, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, program->_locIsEmissiveMapBottomUp, &IsEmissiveBottomUp, SHADER_UNIFORM_INT);
}

void WorldRendererLighting_LogStatus(const WorldRenderer* self)
{
    if ((self->_logger == NULL) || !self->_isStatusReported)
    {
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: light at the camera (%s): sun %.4g %.4g %.4g, moon %.4g %.4g %.4g, sky "
        u8"%.4g %.4g %.4g (irradiance on a surface facing up), ground %.4g %.4g %.4g (facing down), night %.4g %.4g %.4g; "
        u8"scene luminance %.4g, the eye adapted to %.4g (%s), exposure %.3g; the C mirror's tables built %" PRIu32
        u8" times (when the air changes, over frames%s).",
        Status->IsSkyLightPerHeight ? "the surfaces take it at their own heights" : "every surface takes the camera's",
        (double)Status->SunLight[0], (double)Status->SunLight[1], (double)Status->SunLight[2],
        (double)Status->MoonLight[0], (double)Status->MoonLight[1], (double)Status->MoonLight[2],
        (double)Status->SkyIrradiance[0], (double)Status->SkyIrradiance[1], (double)Status->SkyIrradiance[2],
        (double)Status->GroundIrradiance[0], (double)Status->GroundIrradiance[1], (double)Status->GroundIrradiance[2],
        (double)Status->NightLight[0], (double)Status->NightLight[1], (double)Status->NightLight[2],
        (double)Status->SceneLuminance, (double)Status->AdaptedLuminance,
        Status->IsExposureMetered ? "measured" : "the anchor", (double)Status->Exposure,
        Status->SkyLightTableBuildCount,
        Status->IsSkyLightTableBuilding ? "; a rebuild is running" : "");
    Error_Deconstruct(&LogResult);
    // The names as the last render copied them: the world may have changed since. Each light's text holds a whole name.
    char CubeLight[LIGHT_STATUS_TEXT_CAPACITY];
    if (Status->PointShadowObjectId != 0U)
    {
        snprintf(CubeLight, sizeof(CubeLight), "object %" PRIu64 " (\"%s\")", Status->PointShadowObjectId,
            (const char*)self->_pbr._shadowLightNames[0]);
    }
    else
    {
        snprintf(CubeLight, sizeof(CubeLight), "none");
    }
    char SpotLights[WORLD_RENDERER_STATUS_MAX_SPOT_SHADOWS * LIGHT_STATUS_TEXT_CAPACITY];
    size_t SpotLength = 0U;
    SpotLights[0] = '\0';
    for (uint32_t Spot = 0U; (Spot < WORLD_RENDERER_STATUS_MAX_SPOT_SHADOWS) && (SpotLength < sizeof(SpotLights)); Spot++)
    {
        uint64_t Id = Status->SpotShadowObjectIds[Spot];
        if (Id == 0U)
        {
            continue;
        }
        int Written = snprintf(SpotLights + SpotLength, sizeof(SpotLights) - SpotLength, "%sobject %" PRIu64 " (\"%s\")",
            (SpotLength > 0U) ? ", " : "", Id, (const char*)self->_pbr._shadowLightNames[1U + Spot]);
        if (Written < 0)
        {
            break;
        }
        SpotLength += (size_t)Written;
    }
    LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: lights: %zu (%zu points, %zu spots; %zu off, %zu flickering), at most %d "
        u8"a draw; the cube shadow's: %s; the spots' shadows: %s.", Status->LightCount,
        Status->LightCount - Status->SpotLightCount, Status->SpotLightCount, Status->LightOffCount,
        Status->FlickeringLightCount, WORLD_MAX_FORWARD_LIGHTS, CubeLight, (SpotLength > 0U) ? SpotLights : "none");
    Error_Deconstruct(&LogResult);
    LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: weather: the clouds hide %.3g of the sky (the sun and the moon %.3g of their "
        u8"light; they pass %.3g of what falls on them), their deck's light %.4g %.4g %.4g on a level surface, a flash's "
        u8"%.4g %.4g %.4g, wetness %.3g.",
        (double)Status->CloudCover, (double)Status->DirectShare, (double)Status->CloudTransmission,
        (double)Status->DeckIrradiance[0], (double)Status->DeckIrradiance[1], (double)Status->DeckIrradiance[2],
        (double)Status->FlashIrradiance[0], (double)Status->FlashIrradiance[1], (double)Status->FlashIrradiance[2],
        (double)Status->Wetness);
    Error_Deconstruct(&LogResult);
}
