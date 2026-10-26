// The PBR shader and the frame lighting every lit shader (models, sprites, terrain) gets through common/lighting (the sky's
// light: the sun and the moon through the air and the sky's own light as the ambient, T36; the haze, shadows, per-object
// forward point lights and haze, the eye's night vision), the PBR's per-material factors, and HDR eye adaptation.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "WorldLightCulling.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Macros.
/** Asset name of the world PBR (metallic/roughness) shader used to shade model objects. */
#define PBR_SHADER_ASSET_NAME ((const unsigned char*)u8"world_pbr")
/** Asset name of its skinned twin (T29): the same fragment stage over a vertex stage that skins. */
#define PBR_SKINNED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_pbr_skinned")
/** Asset name of its instanced twin (B03): the same fragment stage over a vertex stage that places every copy of a cell
 *  (WorldRendererInstances.c draws with it). */
#define PBR_INSTANCED_SHADER_ASSET_NAME ((const unsigned char*)u8"world_pbr_instanced")
/** Base shadow depth bias (normalized light-clip depth) to combat acne; slope-scaled in the shader. Tuned
 *  for the shadow frustum's near..far range: ~0.0009 * (far-near) is the world-space offset (a few cm here). */
#define SHADOW_BIAS 0.0009f
/** The exposure the sun's disc is drawn for whatever the eye's (T36): the disc's radiance is divided by the exposure and
 *  multiplied by this, so it keeps its look on screen through dusk, when the exposure rises up to
 *  WORLD_SKY_LIGHT_ADAPT_MAX_EXPOSURE (it would turn the setting disc white): the exposure of a clear noon before T36,
 *  so the disc looks as it did then. */
#define SUN_DISC_SCREEN_EXPOSURE 1.08f
/** The most the sun's disc's intensity holds (T36): its radiance, WORLD_ATMOSPHERE_SUN_DISC_RADIANCE times it, stays
 *  within the scene's half floats whatever sun_intensity is (an infinity there times the moon's cover or a sky_tint of
 *  0 would be a NaN). */
#define SUN_DISC_MAX_INTENSITY (WORLD_SKY_LIGHT_MAX_LIGHT / WORLD_ATMOSPHERE_SUN_DISC_RADIANCE)
/** Base world-space depth bias for the point-light shadow comparison; the PBR shader SLOPE-SCALES this (much
 *  larger where the light grazes a surface, a small fraction head-on). Raise if acne (streaky self-shadowing),
 *  lower if peter-panning (the shadow detaches from its caster). */
#define POINT_SHADOW_BIAS 0.35f
// ---- The light before T36 (the A/B, WorldRenderer_SetOneLightScaleEnabled) ----
// TODO: remove this block, ComputeLegacyAmbientColor, PrepareLegacySkyLight and ExposureFromAdaptedLegacy once the owner
// approves T36's light.
/** The night's ambient floor before T36 (LINEAR RGB): the look of a moonless night (the moon added its own fill on top:
 *  MOON_AMBIENT_SHARE). Since T36 the environment's night light (night_light_color, night_light_intensity) is this. */
#define NIGHT_AMBIENT_R 0.004f
#define NIGHT_AMBIENT_G 0.005f
#define NIGHT_AMBIENT_B 0.011f
/** Light pollution's share of the night ambient before T36: at LightPollution 1 the night floor gained this much of the
 *  (linear) LightPollutionColor. Since T36 the sky's irradiance holds light pollution's light in the air. */
#define NIGHT_AMBIENT_LIGHT_POLLUTION_SHARE 0.004f
/** The daytime ambient's scale before T36 (before the night floor). */
#define AMBIENT_SKY_STRENGTH 0.85f
/** The painted daytime ambient before T36 (LINEAR RGB): the blue skylight when the sun is well above the horizon. */
#define AMBIENT_DAY_R 0.15f
#define AMBIENT_DAY_G 0.35f
#define AMBIENT_DAY_B 0.75f
/** The painted twilight ambient before T36 (LINEAR RGB): the warm tint the fill blended toward near the horizon. */
#define AMBIENT_SUNSET_R 0.80f
#define AMBIENT_SUNSET_G 0.28f
#define AMBIENT_SUNSET_B 0.08f
/** How near the horizon (in sun-elevation units) the ambient warmed before T36: a Gaussian of the sun's elevation with
 *  this as its width, gated by the daylight factor. */
#define AMBIENT_SUNSET_BAND 0.15f
/** The moonlit sky's fill before T36: the share of the moonlight that came back from the sky as ambient. Since T36 the
 *  moonlit sky's irradiance is the moon's ambient. */
#define MOON_AMBIENT_SHARE 0.3f
/** The sun's colour before T36 (sRGB): sun_color's old default, which the light before T36 multiplies into the world's
 *  (white by default since T36), so the A/B shows the old warm sun. */
#define LEGACY_SUN_COLOR_R 255U
#define LEGACY_SUN_COLOR_G 244U
#define LEGACY_SUN_COLOR_B 214U
/** The sky's sun before T36: its light on the air was this times sun_intensity, 22 times the surfaces' sun. */
#define LEGACY_SKY_SUN_RADIANCE 22.0f
/** The share of a directional light in the scene luminance before T36. */
#define LEGACY_DIRECTIONAL_ADAPT_WEIGHT 0.5f
/** The exposure before T36: the scene luminance that mapped to exposure 1, the exposure's range and the lowest luminance
 *  it rose to. */
#define LEGACY_ADAPT_REFERENCE_LUMINANCE 0.80f
#define LEGACY_ADAPT_MIN_EXPOSURE 0.5f
#define LEGACY_ADAPT_MAX_EXPOSURE 24.0f
#define LEGACY_ADAPT_MIN_LUMINANCE 0.02f
// ---- Eye adaptation (HDR auto-exposure; its target on T36's scale: WorldSkyLight_GetExposure) ----
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
/** Pi, for the light before T36's ambient expressed as the sky's irradiance (the shaders divide that by pi). */
#define LIGHTING_PI 3.14159265358979323846f
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

    lit->_locLightVP = GetShaderLocation(shader, "lightVP");
    lit->_locShadowMap = GetShaderLocation(shader, "shadowMap");
    lit->_locLightVPFar = GetShaderLocation(shader, "lightVPFar");
    lit->_locShadowMapFar = GetShaderLocation(shader, "shadowMapFar");
    lit->_locShadowFarActive = GetShaderLocation(shader, "shadowFarActive");
    lit->_locShadowStrength = GetShaderLocation(shader, "shadowStrength");
    lit->_locShadowBias = GetShaderLocation(shader, "shadowBias");
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

/* The light before T36 (the A/B): the painted ambient skylight colour (linear RGB), TRACKING the sky's timing, driven
 * by the sun's elevation and the SAME daylight thresholds the sun lighting used (WorldEnvironment_GetDaylightFactor), so
 * the fill light and the sun agreed:
 *   * day  (sun well above the horizon) -> the daytime blue fill, at full brightness;
 *   * twilight (sun near the horizon)   -> blended toward the warm sunrise/sunset colour;
 *   * night (sun below the horizon)     -> the fill fades out to the dim NIGHT_AMBIENT floor.
 * The warm tint is gated by BOTH a narrow band around the horizon (AMBIENT_SUNSET_BAND, a Gaussian of the sun
 * elevation) AND the daylight factor, so it can neither appear while the sun is still high (sky still blue) nor
 * linger once the sun has set — the two bugs the flat sky-colour gradient produced. The hues are authored
 * (AMBIENT_DAY_* / AMBIENT_SUNSET_*), tinted by AmbientSkylightColor (WHITE = neutral) and scaled by
 * AmbientSkylightIntensity x AMBIENT_SKY_STRENGTH; the whole fill fades with the daylight factor. Light pollution
 * raises the night floor a little in its glow's colour (NIGHT_AMBIENT_LIGHT_POLLUTION_SHARE), and the moon raises it by
 * the moonlit sky's fill (MOON_AMBIENT_SHARE of the moonlight, tinted and scaled like the day's skylight). */
static void ComputeLegacyAmbientColor(const WorldEnvironment* environment, float sunElevation, float daylight,
    const float moonColor[3], float moonIntensity, float outRGB[3])
{
    float SunsetT = sunElevation / AMBIENT_SUNSET_BAND;
    float Sunset = expf(-(SunsetT * SunsetT)) * daylight; // warm only near the horizon AND while it is still day
    if (Sunset < 0.0f) { Sunset = 0.0f; }
    if (Sunset > 1.0f) { Sunset = 1.0f; }

    float DayHue[3] = { AMBIENT_DAY_R, AMBIENT_DAY_G, AMBIENT_DAY_B };
    float SunsetHue[3] = { AMBIENT_SUNSET_R, AMBIENT_SUNSET_G, AMBIENT_SUNSET_B };
    float AmbientTint[3];
    WorldRendererCommon_ColorToLinear(environment->AmbientSkylightColor, AmbientTint);
    float NightAmbient[3] = { NIGHT_AMBIENT_R, NIGHT_AMBIENT_G, NIGHT_AMBIENT_B };
    float PollutionColor[3];
    WorldRendererCommon_ColorToLinear(environment->LightPollutionColor, PollutionColor);
    float Pollution = fminf(fmaxf(environment->LightPollution, 0.0f), 1.0f); // fmaxf drops a NaN
    float MoonFill = moonIntensity * MOON_AMBIENT_SHARE * environment->AmbientSkylightIntensity;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        NightAmbient[Channel] += PollutionColor[Channel] * Pollution * NIGHT_AMBIENT_LIGHT_POLLUTION_SHARE
            + moonColor[Channel] * AmbientTint[Channel] * MoonFill;
    }
    float AmbientMagnitude = daylight * environment->AmbientSkylightIntensity * AMBIENT_SKY_STRENGTH;

    for (int Channel = 0; Channel < 3; Channel++)
    {
        float Hue = DayHue[Channel] + (SunsetHue[Channel] - DayHue[Channel]) * Sunset;
        float Lit = Hue * AmbientTint[Channel] * AmbientMagnitude;
        outRGB[Channel] = fmaxf(Lit, NightAmbient[Channel]);
    }
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

/* Eases the adapted luminance toward the scene luminance over @p dt seconds. Slow in general, but faster the
 * larger the brightness change, and faster still when BRIGHTENING (facing a light) than when dark-adapting —
 * so a jump from a black night to a bright light snaps down quickly while dark-adaptation stays gradual. A
 * zero, negative or NaN delta holds the adaptation. */
static float AdaptLuminance(float adapted, float target, float dt)
{
    if (!(dt > 0.0f))
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

/* The light before T36 (the A/B): its exposure for an adapted luminance (reference/adapted, clamped). */
static float ExposureFromAdaptedLegacy(float adapted)
{
    float LuminanceValue = (adapted < LEGACY_ADAPT_MIN_LUMINANCE) ? LEGACY_ADAPT_MIN_LUMINANCE : adapted;
    float Exposure = LEGACY_ADAPT_REFERENCE_LUMINANCE / LuminanceValue;
    if (Exposure < LEGACY_ADAPT_MIN_EXPOSURE) { Exposure = LEGACY_ADAPT_MIN_EXPOSURE; }
    if (Exposure > LEGACY_ADAPT_MAX_EXPOSURE) { Exposure = LEGACY_ADAPT_MAX_EXPOSURE; }
    return Exposure;
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

/* The light before T36 (the A/B) in the frame's fields: the sun's colour (the world's times the old warm default) at
 * its intensity ramped by the daylight, the moon ramped by its height, the painted ambient (the sky's and the ground's
 * irradiance pi times it, so the shaders' ambient is it), the sky lit by 22 times the sun, the old scene luminance, night
 * vision and shadow light.
 * TODO: remove once the owner approves T36's light. */
static void PrepareLegacySkyLight(WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;
    float WorldSun[3];
    float LegacySun[3];
    WorldRendererCommon_ColorToLinear(Environment->SunColor, WorldSun);
    WorldRendererCommon_ColorToLinear((Color){ LEGACY_SUN_COLOR_R, LEGACY_SUN_COLOR_G, LEGACY_SUN_COLOR_B, 255 },
        LegacySun);
    float SunIntensity = frame->SunIntensity * frame->Daylight;
    float MoonIntensity = frame->MoonIntensity * WORLD_RENDERER_LEGACY_MOON_INTENSITY_SCALE
        * WorldEnvironment_GetMoonlightForElevation(frame->MoonDirection.y);
    ComputeLegacyAmbientColor(Environment, frame->SunDirection.y, frame->Daylight, frame->MoonColor, MoonIntensity,
        frame->LegacyAmbientColor);
    // The sky before T36 took the sun at 22 times sun_intensity and the moonlit sky's share (MoonSkyIntensity), white;
    // its disc was the same sun in the sun's colour.
    float SkyScale = LEGACY_SKY_SUN_RADIANCE / (float)WORLD_ATMOSPHERE_SUN_LIGHT;
    float DiscScale = LEGACY_SKY_SUN_RADIANCE / (float)WORLD_ATMOSPHERE_SUN_DISC_RADIANCE;
    WorldSkyLightFrame* Light = &frame->SkyLight;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        float SunColor = WorldSun[Channel] * LegacySun[Channel];
        Light->Sun[Channel] = SunColor * SunIntensity;
        Light->Moon[Channel] = frame->MoonColor[Channel] * MoonIntensity;
        Light->Sky[Channel] = LIGHTING_PI * frame->LegacyAmbientColor[Channel];
        Light->SunSky[Channel] = Light->Sky[Channel];
        Light->Ground[Channel] = Light->Sky[Channel];
        Light->AmbientTint[Channel] = 1.0f;
        Light->Night[Channel] = 0.0f;
        frame->AirSunIntensity[Channel] = SkyScale * frame->SunIntensity;
        frame->AirMoonIntensity[Channel] = SkyScale * frame->MoonSkyIntensity;
        Light->SunAboveAir[Channel] = (float)WORLD_ATMOSPHERE_SUN_LIGHT * frame->AirSunIntensity[Channel];
        Light->MoonAboveAir[Channel] = (float)WORLD_ATMOSPHERE_SUN_LIGHT * frame->AirMoonIntensity[Channel];
        frame->SunDiscIntensity[Channel] = SunColor * frame->SunIntensity * DiscScale;
    }
    frame->IsSkyLightPerHeight = false;
    frame->IsMoonLit = MoonIntensity > 0.0f;
    frame->BaseSceneLuminance = Luminance(frame->LegacyAmbientColor)
        + Luminance(Light->Sun) * LEGACY_DIRECTIONAL_ADAPT_WEIGHT + Luminance(Light->Moon) * LEGACY_DIRECTIONAL_ADAPT_WEIGHT;
    frame->NightVisionField = frame->BaseSceneLuminance;
    frame->NightVision = self->_legacyNightVision;
    ChooseShadowLight(frame, Luminance(Light->Sun), Luminance(Light->Moon));
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

    // Constant shadow parameters; upload once.
    float ShadowBias = SHADOW_BIAS;
    float ShadowTexel = 1.0f / (float)SHADOW_MAP_SIZE;
    SetShaderValue(shader, outLocations->_locShadowBias, &ShadowBias, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, outLocations->_locShadowTexelSize, &ShadowTexel, SHADER_UNIFORM_FLOAT);

    // Point the renderer-owned samplers at their fixed units once (see WorldRendererTextureUnit).
    AssignSamplerUnit(shader, outLocations->_locShadowMap, WorldRendererTextureUnit_ShadowNear);
    AssignSamplerUnit(shader, outLocations->_locShadowMapFar, WorldRendererTextureUnit_ShadowFar);
    AssignSamplerUnit(shader, outLocations->_locPointShadowCube, WorldRendererTextureUnit_PointShadowCube);
    AssignSamplerUnit(shader, outLocations->_locAmbientOcclusionMap, WorldRendererTextureUnit_AmbientOcclusion);
    // The air (T15) and the haze (T35: the volume's sampler on its own unit).
    WorldRendererAtmosphere_PrepareShader(shader, &outLocations->_atmosphere);
    WorldRendererAtmosphere_PrepareHazeShader(shader, true, &outLocations->_haze);

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
    if (!frame->IsOneLightScale)
    {
        PrepareLegacySkyLight(self, frame);
        return;
    }

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
    // The sun's disc for the eye's exposure (the last render's: this one's is set after the scene is drawn).
    float Exposure = WorldSkyLight_GetExposure(self->_exposure._isInitialized ? self->_exposure._adaptedLuminance
        : frame->BaseSceneLuminance);
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
    // Night vision reads the daylight on the real scale and the night's lights (raised for play) through its power law.
    float DayLuminance = WorldSkyLight_GetDayLuminance(Light);
    frame->NightVisionField = NightVision_GetFieldLuminance(DayLuminance, frame->BaseSceneLuminance - DayLuminance);
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
    // volume's last row, or the camera's from these uniforms (the C mirror's; the A/B's light before T36).
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

    // The haze in front of every surface (T35): the volume the atmosphere pass built for the scene camera's screen
    // directions (read through the scene's view-projection, also by the crisp pass), the frame's haze and the ground
    // haze. Each draw sets its own multiplier; 1 until the first does.
    WorldRendererAtmosphere_UploadFrame(shader, &lit->_atmosphere, frame);
    WorldRendererAtmosphere_UploadHaze(shader, &lit->_haze, frame);
    SetShaderValueMatrix(shader, lit->_locHazeViewProjection, frame->ViewProjection);
    WorldRendererLighting_SetDrawHaze(shader, lit, 1.0f);

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
    int AoMapActive = (applyScreenAo && frame->IsAoActive) ? 1 : 0;
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
    if (applyScreenAo && frame->IsAoActive)
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

void WorldRendererLighting_SetDrawHaze(Shader shader, const WorldRendererLitLocations* locations, float haze)
{
    SetShaderValue(shader, locations->_locHazeMultiplier, &haze, SHADER_UNIFORM_FLOAT);
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

/* Starts from the frame's base scene luminance (ambient + sun + moon), adds nearby point lights (being near a light
 * raises the estimate -> lowers exposure; in render space, the camera at the origin), then eases the adapted luminance
 * toward it. A non-finite estimate (a corrupt light or environment value) holds the adaptation, so it can never be
 * poisoned for good. */
float WorldRendererLighting_UpdateExposure(WorldRenderer* self, const WorldRenderFrame* frame, float deltaSeconds)
{
    float SceneLuminance = frame->BaseSceneLuminance;

    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        const WorldRenderLight* Light = &Lights[Index];
        if ((Light->Range <= 0.0f) || (Light->Intensity <= 0.0f))
        {
            continue;
        }
        float Distance = Vector3Length(Light->RenderPosition);
        if (Distance >= Light->Range)
        {
            continue;
        }
        float LinearColor[3];
        WorldRendererCommon_ColorToLinear(Light->Color, LinearColor);
        float Proximity = 1.0f - (Distance / Light->Range);
        SceneLuminance += Luminance(LinearColor) * Light->Intensity * Proximity * Proximity * POINT_ADAPT_WEIGHT;
    }

    if (!isfinite(SceneLuminance))
    {
        SceneLuminance = self->_exposure._isInitialized ? self->_exposure._adaptedLuminance
            : WORLD_SKY_LIGHT_ADAPT_REFERENCE_LUMINANCE;
    }
    if (!self->_exposure._isInitialized || !isfinite(self->_exposure._adaptedLuminance))
    {
        self->_exposure._adaptedLuminance = SceneLuminance; // snap on the first frame so there is no start-up flash
        self->_exposure._isInitialized = true;
    }
    else
    {
        self->_exposure._adaptedLuminance = AdaptLuminance(self->_exposure._adaptedLuminance, SceneLuminance,
            deltaSeconds);
    }
    // TODO: remove the light before T36's exposure once the owner approves T36's light.
    return frame->IsOneLightScale ? WorldSkyLight_GetExposure(self->_exposure._adaptedLuminance)
        : ExposureFromAdaptedLegacy(self->_exposure._adaptedLuminance);
}

void WorldRendererLighting_LogStatus(const WorldRenderer* self)
{
    if ((self->_logger == NULL) || !self->_isStatusReported)
    {
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: light %s at the camera (%s): sun %.4g %.4g %.4g, moon %.4g %.4g %.4g, sky "
        u8"%.4g %.4g %.4g (irradiance on a surface facing up), ground %.4g %.4g %.4g (facing down), night %.4g %.4g %.4g; "
        u8"scene luminance %.4g, exposure %.3g; the C mirror's tables built %" PRIu32 u8" times (when the air changes, "
        u8"over frames%s).",
        Status->IsOneLightScale ? "T36 (one scale for the sky and the surfaces)" : "before T36 (the A/B)",
        Status->IsSkyLightPerHeight ? "the surfaces take it at their own heights" : "every surface takes the camera's",
        (double)Status->SunLight[0], (double)Status->SunLight[1], (double)Status->SunLight[2],
        (double)Status->MoonLight[0], (double)Status->MoonLight[1], (double)Status->MoonLight[2],
        (double)Status->SkyIrradiance[0], (double)Status->SkyIrradiance[1], (double)Status->SkyIrradiance[2],
        (double)Status->GroundIrradiance[0], (double)Status->GroundIrradiance[1], (double)Status->GroundIrradiance[2],
        (double)Status->NightLight[0], (double)Status->NightLight[1], (double)Status->NightLight[2],
        (double)Status->SceneLuminance, (double)Status->Exposure, Status->SkyLightTableBuildCount,
        Status->IsSkyLightTableBuilding ? "; a rebuild is running" : "");
    Error_Deconstruct(&LogResult);
}
