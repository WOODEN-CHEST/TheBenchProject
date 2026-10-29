// The PBR shader and the frame lighting every lit shader (models, sprites, terrain) gets through common/lighting (the sky's
// light: the sun and the moon through the air and the sky's own light as the ambient; the haze, shadows, per-object
// forward point lights and each draw's place: its haze and its ambient zones; the eye's night vision), and the PBR's
// per-material factors. The eye's adaptation is WorldRendererExposure.c's; interior lighting's parts are
// WorldRendererInterior.c's and WorldRendererSkyVisibility.c's.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "WorldLightCulling.h"
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
/** The cascades' depth bias and normal offset, in texels of each cascade: half a texel of depth (the maps hold the
 *  casters' faces turned away from the light, so a lit surface never meets itself there), and the lookup moved off the
 *  surface along its normal by a texel and a half at grazing incidence (no surface shadows itself through the map's
 *  texels at a slant). In the near cascade that is 0.8 cm and 2.3 cm: a shadow reaches the foot of what casts it, and a
 *  roof's shadow the top of the wall under it. */
#define SHADOW_DEPTH_BIAS_TEXELS 0.5f
#define SHADOW_NORMAL_OFFSET_TEXELS 1.5f
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
/** Room for the name of one point-light array element ("pointLightPositions[N]") when probing the shader. */
#define POINT_LIGHT_UNIFORM_NAME_CAPACITY 48


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

/* How many entries the shader's point-light arrays have, up to WORLD_MAX_FORWARD_LIGHTS: the highest N whose
 * element pointLightPositions[N-1] has a location (the loop indexes the arrays dynamically, so every element of
 * the declared size is active). 0 when the arrays are absent. */
static int CountShaderPointLights(Shader shader)
{
    for (int Count = WORLD_MAX_FORWARD_LIGHTS; Count > 0; Count--)
    {
        char Name[POINT_LIGHT_UNIFORM_NAME_CAPACITY];
        snprintf(Name, sizeof(Name), "pointLightPositions[%d]", Count - 1);
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

    lit->_locAmbientOcclusionMap = GetShaderLocation(shader, "ambientOcclusionMap");
    lit->_locAoMapActive = GetShaderLocation(shader, "aoMapActive");
    lit->_locAoResolution = GetShaderLocation(shader, "aoResolution");
    lit->_locSkyVisibilityOnScreen = GetShaderLocation(shader, "skyVisibilityOnScreen");
    lit->_locDrawInPrepass = GetShaderLocation(shader, "drawInPrepass");

    lit->_locLightVP = GetShaderLocation(shader, "lightVP");
    lit->_locShadowMap = GetShaderLocation(shader, "shadowMap");
    lit->_locLightVPFar = GetShaderLocation(shader, "lightVPFar");
    lit->_locShadowMapFar = GetShaderLocation(shader, "shadowMapFar");
    lit->_locShadowFarActive = GetShaderLocation(shader, "shadowFarActive");
    lit->_locShadowStrength = GetShaderLocation(shader, "shadowStrength");
    lit->_locShadowDepthBias = GetShaderLocation(shader, "shadowDepthBias");
    lit->_locShadowNormalOffset = GetShaderLocation(shader, "shadowNormalOffset");
    lit->_locShadowTexelSize = GetShaderLocation(shader, "shadowTexelSize");
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

    lit->_locPointLightCount = GetShaderLocation(shader, "pointLightCount");
    lit->_locPointLightPositions = GetShaderLocation(shader, "pointLightPositions");
    lit->_locPointLightRadiances = GetShaderLocation(shader, "pointLightRadiances");
    lit->_locPointLightRanges = GetShaderLocation(shader, "pointLightRanges");

    lit->_locHazeViewProjection = GetShaderLocation(shader, "hazeViewProjection");
    lit->_locHazeMultiplier = GetShaderLocation(shader, "hazeMultiplier");

    lit->_locNightVisionFieldLuminance = GetShaderLocation(shader, "nightVisionFieldLuminance");
    lit->_locNightVisionHalfLuminance = GetShaderLocation(shader, "nightVisionHalfLuminance");
    lit->_locNightVisionSteepness = GetShaderLocation(shader, "nightVisionSteepness");
    lit->_locNightVisionStrength = GetShaderLocation(shader, "nightVisionStrength");
    lit->_locNightVisionTint = GetShaderLocation(shader, "nightVisionTint");
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
}

/* Loads one PBR program and prepares it: its lit locations and constants, its material locations, its dither, its own
 * location table and pass shader, and its palette (none in the rigid program). */
static void LoadPbrProgram(WorldRenderer* self, WorldRendererPbrProgram* program, const unsigned char* assetName)
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

/* Builds this render's forward-light candidates and their linear radiance (colour -> linear x intensity, once
 * per light per frame): every light of the render cache that can light anything, except the cube-shadow light,
 * which the shader shades separately with its shadow. In render space, like the spheres they are culled against. */
static void BuildForwardLights(WorldRenderer* self, const WorldRenderFrame* frame)
{
    GenericBuffer_Clear(&self->_pbr._forwardLights);
    GenericBuffer_Clear(&self->_pbr._forwardRadiances);

    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        const WorldRenderLight* Light = &Lights[Index];
        if ((frame->IsPointShadowActive && (Index == frame->ShadowLightIndex))
            || (Light->Range <= 0.0f) || (Light->Intensity <= 0.0f))
        {
            continue;
        }

        WorldCullingLight Candidate =
        {
            .Position = Light->RenderPosition,
            .Range = Light->Range,
            .Intensity = Light->Intensity
        };
        WorldRendererRadiance Radiance;
        WorldRendererCommon_ColorToLinear(Light->Color, Radiance.Rgb);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            Radiance.Rgb[Channel] *= Light->Intensity;
        }
        if (!GenericBuffer_AddLast(&self->_pbr._forwardLights, &Candidate)
            || !GenericBuffer_AddLast(&self->_pbr._forwardRadiances, &Radiance))
        {
            // Cannot happen short of address-space exhaustion; keep the two lists parallel by dropping both.
            GenericBuffer_Clear(&self->_pbr._forwardLights);
            GenericBuffer_Clear(&self->_pbr._forwardRadiances);
            return;
        }
    }
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
void WorldRendererLighting_LoadShader(WorldRenderer* self)
{
    LoadPbrProgram(self, &self->_pbr._rigid, PBR_SHADER_ASSET_NAME);
    if (self->_pbr._rigid._shader == NULL)
    {
        return; // skinned meshes need the lit pass too
    }
    LoadPbrProgram(self, &self->_pbr._skinned, PBR_SKINNED_SHADER_ASSET_NAME);
    if ((self->_pbr._skinned._shader != NULL) && (self->_pbr._skinned._palette._location < 0))
    {
        WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: \"world_pbr_skinned\" has no bone "
            u8"palette (bonePalette); skinned models draw in their bind pose.");
    }
    LoadPbrProgram(self, &self->_pbr._instanced, PBR_INSTANCED_SHADER_ASSET_NAME);
}

void WorldRendererLighting_PrepareLitShader(WorldRenderer* self, Shader shader, const unsigned char* assetName,
    WorldRendererLitLocations* outLocations)
{
    CacheLitLocations(outLocations, shader);

    // Constant shadow parameters; upload once. Each cascade's by its texel's world size (the depth bias in its depth
    // units: the light frustum's near to far plane is 0 to 1).
    float ShadowTexel = 1.0f / (float)SHADOW_MAP_SIZE;
    float TexelWorld[2] = { SHADOW_NEAR_ORTHO_SIZE * ShadowTexel, SHADOW_FAR_ORTHO_SIZE * ShadowTexel };
    float DepthBias[2];
    float NormalOffset[2];
    for (int Cascade = 0; Cascade < 2; Cascade++)
    {
        DepthBias[Cascade] = SHADOW_DEPTH_BIAS_TEXELS * TexelWorld[Cascade] / (SHADOW_FAR - SHADOW_NEAR);
        NormalOffset[Cascade] = SHADOW_NORMAL_OFFSET_TEXELS * TexelWorld[Cascade];
    }
    SetShaderValue(shader, outLocations->_locShadowDepthBias, DepthBias, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, outLocations->_locShadowNormalOffset, NormalOffset, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, outLocations->_locShadowTexelSize, &ShadowTexel, SHADER_UNIFORM_FLOAT);

    // Point the renderer-owned samplers at their fixed units once (see WorldRendererTextureUnit).
    AssignSamplerUnit(shader, outLocations->_locShadowMap, WorldRendererTextureUnit_ShadowNear);
    AssignSamplerUnit(shader, outLocations->_locShadowMapFar, WorldRendererTextureUnit_ShadowFar);
    AssignSamplerUnit(shader, outLocations->_locPointShadowCube, WorldRendererTextureUnit_PointShadowCube);
    AssignSamplerUnit(shader, outLocations->_locAmbientOcclusionMap, WorldRendererTextureUnit_AmbientOcclusion);
    // The air and the haze (the volume's sampler on its own unit), and interior lighting (the sky's visibility map on
    // its own).
    WorldRendererAtmosphere_PrepareShader(shader, &outLocations->_atmosphere);
    WorldRendererAtmosphere_PrepareHazeShader(shader, true, &outLocations->_haze);
    WorldRendererInterior_PrepareShader(shader, &outLocations->_interior);

    // The shader's point-light arrays must hold the WORLD_MAX_FORWARD_LIGHTS the culling may select; with fewer,
    // upload only as many as fit (the shader also clamps its loop to its own array size).
    outLocations->_maxPointLights = CountShaderPointLights(shader);
    if ((outLocations->_maxPointLights < WORLD_MAX_FORWARD_LIGHTS) && (self->_logger != NULL))
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"WorldRenderer: \"%s\" holds %d point lights, fewer than the %d the renderer "
            u8"selects (WORLD_MAX_FORWARD_LIGHTS); objects get at most %d.",
            (const char*)assetName, outLocations->_maxPointLights, WORLD_MAX_FORWARD_LIGHTS,
            outLocations->_maxPointLights);
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
    WorldSkyLight_GetFrameLight(Atmosphere->_skyLight.Tables, &frame->AtmosphereMedium, Environment, &Lights,
        frame->LightPollutionRadiance, (double)frame->AtmosphereViewHeight, &frame->SkyLight);
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
    for (int Channel = 0; Channel < 3; Channel++)
    {
        frame->AirSunIntensity[Channel] = Light->SunAboveAir[Channel] / (float)WORLD_ATMOSPHERE_SUN_LIGHT;
        frame->AirMoonIntensity[Channel] = Light->MoonAboveAir[Channel] / (float)WORLD_ATMOSPHERE_SUN_LIGHT;
        // fminf takes an absurd intensity's infinity to the most the disc holds.
        frame->SunDiscIntensity[Channel] = fminf(SunColor[Channel] * fmaxf(frame->SunIntensity, 0.0f)
            * SUN_DISC_SCREEN_EXPOSURE / Exposure, (float)SUN_DISC_MAX_INTENSITY);
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

    // shadowStrength is shared by the cascades' and the point shadow's visibility, so it is gated on global shadow
    // enablement (NOT the sun being up) — otherwise a point light would cast no shadow at night. Which directional
    // light the cascades darken, and how much, is the shader's sunShadowFade / moonShadowFade (0 for both while the
    // cascades do not run, so a stale map is never sampled).
    float EffectiveShadowStrength = Environment->ShadowStrength * WorldRendererCommon_ConfigShadowStrength(self);
    frame->ShadowStrength = (Environment->AreShadowsEnabled && (EffectiveShadowStrength > 0.0f))
        ? EffectiveShadowStrength : 0.0f;

    BuildForwardLights(self, frame);
}

/* The cube-shadow light of the frame, or NULL while the cube shadow does not run. */
static const WorldRenderLight* GetPointShadowLight(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    return (frame->IsPointShadowActive && (frame->ShadowLightIndex < LightCount)) ? &Lights[frame->ShadowLightIndex]
        : NULL;
}

/* Uploads the frame's lighting values to one lit shader (common/lighting): the sun and the moon, the ambient, the haze,
 * the night vision, the cascades' fades, light direction and matrices, the AO switch and the cube-shadow light.
 * Positions and matrices are in render space, where the camera sits at the origin (so it needs no uniform). The
 * textures are bound once for every lit shader (BindFrameTextures). */
static void UploadLitUniforms(WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererLitLocations* lit, bool applyScreenAo)
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
    SetShaderValue(shader, lit->_locSkyIrradiance, Light->Sky, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, lit->_locGroundIrradiance, Light->Ground, SHADER_UNIFORM_VEC3);
    int MoonLit = frame->IsMoonLit ? 1 : 0;
    SetShaderValue(shader, lit->_locIsMoonLit, &MoonLit, SHADER_UNIFORM_INT);
    SetShaderValue(shader, lit->_locAmbientTint, Light->AmbientTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, lit->_locNightLight, Light->Night, SHADER_UNIFORM_VEC3);

    // The haze in front of every surface: the volume the atmosphere pass built for the scene camera's screen
    // directions (read through the scene's view-projection, also by the crisp pass), the frame's haze and the ground
    // haze. Each draw sets its own multiplier and zones; 1 and none until the first does.
    WorldRendererAtmosphere_UploadFrame(shader, &lit->_atmosphere, frame);
    WorldRendererAtmosphere_UploadHaze(shader, &lit->_haze, frame);
    SetShaderValueMatrix(shader, lit->_locHazeViewProjection, frame->ViewProjection);
    float NoHazeChange = 1.0f;
    SetShaderValue(shader, lit->_locHazeMultiplier, &NoHazeChange, SHADER_UNIFORM_FLOAT);
    // Interior lighting: the sky's visibility map and the debug view.
    WorldRendererInterior_UploadFrame(self, frame, shader, &lit->_interior);

    // The eye's night vision for the reflected light and the haze (strength 0 leaves every colour as it is), calibrated on
    // the frame's light scale.
    SetShaderValue(shader, lit->_locNightVisionFieldLuminance, &frame->NightVisionFieldLuminance, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locNightVisionHalfLuminance, &frame->NightVision.HalfLuminance, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locNightVisionSteepness, &frame->NightVision.Steepness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locNightVisionStrength, &frame->NightVisionStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locNightVisionTint, frame->NightVisionTint, SHADER_UNIFORM_VEC3);

    // The cascades darken the light they were rendered from (the sun or the moon), by the frame's shadow fade; with
    // both fades 0 (the cascades did not run) the shader samples no shadow map at all. Their light matrices go up when
    // they ran this frame: the near cascade always, the far one only when its map exists.
    float SunShadowFade = 0.0f;
    float MoonShadowFade = 0.0f;
    if (frame->IsCascadeShadowActive)
    {
        SunShadowFade = (frame->ShadowLight == WorldShadowLight_Sun) ? frame->ShadowFade : 0.0f;
        MoonShadowFade = (frame->ShadowLight == WorldShadowLight_Moon) ? frame->ShadowFade : 0.0f;
    }
    SetShaderValue(shader, lit->_locSunShadowFade, &SunShadowFade, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, lit->_locMoonShadowFade, &MoonShadowFade, SHADER_UNIFORM_FLOAT);
    float ShadowDir[3] = { frame->ShadowDirection.x, frame->ShadowDirection.y, frame->ShadowDirection.z };
    SetShaderValue(shader, lit->_locShadowLightDirection, ShadowDir, SHADER_UNIFORM_VEC3);
    if (frame->IsCascadeShadowActive)
    {
        SetShaderValueMatrix(shader, lit->_locLightVP, frame->NearCascade.ViewProjection);
        int ShadowFarActive = self->_cascades._hasFarMap ? 1 : 0;
        SetShaderValue(shader, lit->_locShadowFarActive, &ShadowFarActive, SHADER_UNIFORM_INT);
        if (self->_cascades._hasFarMap)
        {
            SetShaderValueMatrix(shader, lit->_locLightVPFar, frame->FarCascade.ViewProjection);
        }
    }

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

        float Radiance[3];
        WorldRendererCommon_ColorToLinear(PointShadowLight->Color, Radiance);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            Radiance[Channel] *= PointShadowLight->Intensity;
        }
        SetShaderValue(shader, lit->_locPointShadowLightRadiance, Radiance, SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, lit->_locPointShadowLightRange, &PointShadowLight->Range, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, lit->_locPointShadowFar, &frame->PointShadowFar, SHADER_UNIFORM_FLOAT);
        float PointBias = POINT_SHADOW_BIAS;
        SetShaderValue(shader, lit->_locPointShadowBias, &PointBias, SHADER_UNIFORM_FLOAT);
    }

    SetShaderValue(shader, lit->_locShadowStrength, &frame->ShadowStrength, SHADER_UNIFORM_FLOAT);
}

/* Binds the frame's shadow maps, AO map, shadow cube and the haze's volume to their fixed units (the lit shaders'
 * samplers point there), once for every lit shader: texture units are context state. Bound manually, which works
 * because the lit passes draw with DrawMesh (a direct draw, not the 2D batch). */
static void BindFrameTextures(WorldRenderer* self, const WorldRenderFrame* frame, bool applyScreenAo)
{
    WorldRendererAtmosphere_BindVolume(self, frame);
    WorldRendererSkyVisibility_BindMap(self, frame);
    if (frame->IsCascadeShadowActive)
    {
        rlActiveTextureSlot(WorldRendererTextureUnit_ShadowNear);
        rlEnableTexture(self->_cascades._nearMap.depth.id);
        if (self->_cascades._hasFarMap)
        {
            rlActiveTextureSlot(WorldRendererTextureUnit_ShadowFar);
            rlEnableTexture(self->_cascades._farMap.depth.id);
        }
    }
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
    bool applyScreenAo, Vector2 ditherPixelScale)
{
    if (program->_shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(program->_shader);
    UploadLitUniforms(self, frame, RayShader, &program->_lit, applyScreenAo);
    WorldRendererCommon_SetDitherPixelScale(RayShader, &program->_dither, ditherPixelScale);
}

void WorldRendererLighting_UploadFrame(WorldRenderer* self, const WorldRenderFrame* frame, bool applyScreenAo,
    Vector2 ditherPixelScale)
{
    bool HasPbr = (self->_pbr._rigid._shader != NULL);
    bool HasSprites = (self->_sprites._shader != NULL);
    bool HasTerrain = (self->_terrain._shader != NULL);
    UploadPbrFrame(self, frame, &self->_pbr._rigid, applyScreenAo, ditherPixelScale);
    UploadPbrFrame(self, frame, &self->_pbr._skinned, applyScreenAo, ditherPixelScale);
    UploadPbrFrame(self, frame, &self->_pbr._instanced, applyScreenAo, ditherPixelScale);
    if (HasSprites)
    {
        Shader SpriteShader = GameShader_GetRaylibShader(self->_sprites._shader);
        UploadLitUniforms(self, frame, SpriteShader, &self->_sprites._lit, applyScreenAo);
        WorldRendererCommon_SetDitherPixelScale(SpriteShader, &self->_sprites._dither, ditherPixelScale);
    }
    if (HasTerrain)
    {
        Shader TerrainShader = GameShader_GetRaylibShader(self->_terrain._shader);
        UploadLitUniforms(self, frame, TerrainShader, &self->_terrain._lit, applyScreenAo);
        WorldRendererCommon_SetDitherPixelScale(TerrainShader, &self->_terrain._dither, ditherPixelScale);
    }
    if (HasPbr || HasSprites || HasTerrain)
    {
        BindFrameTextures(self, frame, applyScreenAo);
    }
}

void WorldRendererLighting_UploadObjectLights(WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, Vector3 center, float radius)
{
    if (locations->_locPointLightCount < 0)
    {
        return;
    }

    const void* CandidateData = self->_pbr._forwardLights._data;
    const void* RadianceData = self->_pbr._forwardRadiances._data;
    const WorldCullingLight* Candidates = CandidateData;
    const WorldRendererRadiance* Radiances = RadianceData;

    size_t Selected[WORLD_MAX_FORWARD_LIGHTS];
    size_t Count = WorldLightCulling_SelectForSphere(Candidates, self->_pbr._forwardLights._count, center, radius,
        Selected, (size_t)locations->_maxPointLights);

    float Positions[WORLD_MAX_FORWARD_LIGHTS * 3];
    float RadianceValues[WORLD_MAX_FORWARD_LIGHTS * 3];
    float Ranges[WORLD_MAX_FORWARD_LIGHTS];
    for (size_t Index = 0; Index < Count; Index++)
    {
        const WorldCullingLight* Light = &Candidates[Selected[Index]];
        const WorldRendererRadiance* Radiance = &Radiances[Selected[Index]];
        Positions[Index*3 + 0] = Light->Position.x;
        Positions[Index*3 + 1] = Light->Position.y;
        Positions[Index*3 + 2] = Light->Position.z;
        RadianceValues[Index*3 + 0] = Radiance->Rgb[0];
        RadianceValues[Index*3 + 1] = Radiance->Rgb[1];
        RadianceValues[Index*3 + 2] = Radiance->Rgb[2];
        Ranges[Index] = Light->Range;
    }

    int CountInt = (int)Count;
    SetShaderValue(shader, locations->_locPointLightCount, &CountInt, SHADER_UNIFORM_INT);
    if (Count > 0)
    {
        SetShaderValueV(shader, locations->_locPointLightPositions, Positions, SHADER_UNIFORM_VEC3, CountInt);
        SetShaderValueV(shader, locations->_locPointLightRadiances, RadianceValues, SHADER_UNIFORM_VEC3, CountInt);
        SetShaderValueV(shader, locations->_locPointLightRanges, Ranges, SHADER_UNIFORM_FLOAT, CountInt);
    }
}

void WorldRendererLighting_SetDrawPlace(const WorldRenderer* self, Shader shader,
    const WorldRendererLitLocations* locations, Vector3 center, float radius, float haze, bool isInPrepass)
{
    SetShaderValue(shader, locations->_locHazeMultiplier, &haze, SHADER_UNIFORM_FLOAT);
    int InPrepass = isInPrepass ? 1 : 0;
    SetShaderValue(shader, locations->_locDrawInPrepass, &InPrepass, SHADER_UNIFORM_INT);
    WorldRendererInterior_UploadZones(self, shader, &locations->_interior, center, radius);
}

/* raylib's DrawMesh uploads colDiffuse and binds the material's textures (via the map-sampler locations of the
 * pass shader's table), but NOT the scalar factors or the has*Map guards, so this does that. A map flag is true
 * only when the slot actually holds a texture; the shader must not sample an unbound map sampler (it would read
 * texture unit 0 = albedo — the silent-fallback bug seen across the post passes). */
void WorldRendererLighting_UploadMaterial(const WorldRendererPbrProgram* program, const Material* material)
{
    Shader RayShader = GameShader_GetRaylibShader(program->_shader);
    float Metallic = material->maps[MATERIAL_MAP_METALNESS].value;
    float Roughness = material->maps[MATERIAL_MAP_ROUGHNESS].value;
    float Ao = material->maps[MATERIAL_MAP_OCCLUSION].value;
    float EmissiveIntensity = material->maps[MATERIAL_MAP_EMISSION].value;
    float EmissiveColor[3];
    WorldRendererCommon_ColorToLinear(material->maps[MATERIAL_MAP_EMISSION].color, EmissiveColor);

    int HasMra = (material->maps[MATERIAL_MAP_METALNESS].texture.id > 0) ? 1 : 0;
    int HasNormal = (material->maps[MATERIAL_MAP_NORMAL].texture.id > 0) ? 1 : 0;
    int HasEmissive = (material->maps[MATERIAL_MAP_EMISSION].texture.id > 0) ? 1 : 0;

    SetShaderValue(RayShader, program->_locMetallic, &Metallic, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locRoughness, &Roughness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locAo, &Ao, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locEmissiveColor, EmissiveColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, program->_locEmissiveIntensity, &EmissiveIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, program->_locHasMraMap, &HasMra, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, program->_locHasNormalMap, &HasNormal, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, program->_locHasEmissiveMap, &HasEmissive, SHADER_UNIFORM_INT);
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
}
