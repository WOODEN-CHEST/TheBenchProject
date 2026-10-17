// The PBR shader and its uniforms (sun, ambient, fog, shadows, per-object forward point lights, per-material
// factors) and HDR eye adaptation.

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
/** Base shadow depth bias (normalized light-clip depth) to combat acne; slope-scaled in the shader. Tuned
 *  for the shadow frustum's near..far range: ~0.0009 * (far-near) is the world-space offset (a few cm here). */
#define SHADOW_BIAS 0.0009f
/** Base world-space depth bias for the point-light shadow comparison; the PBR shader SLOPE-SCALES this (much
 *  larger where the light grazes a surface, a small fraction head-on). Raise if acne (streaky self-shadowing),
 *  lower if peter-panning (the shadow detaches from its caster). */
#define POINT_SHADOW_BIAS 0.35f
/** ===== NIGHT DARKNESS KNOB (1 of 2) =====
 *  Dim cool night-ambient floor (LINEAR RGB): the darkest the sky-derived ambient is allowed to get at night.
 *  Kept just above 0 (a faint blue, like moonlight) so night is not pure black and the eye adaptation has a
 *  little light to lift. LOWER these to make the night DARKER (and less tinted); raise them to brighten it.
 *  Works together with ADAPT_MAX_EXPOSURE below — the perceived night brightness is roughly this floor's
 *  luminance times that exposure cap. */
#define NIGHT_AMBIENT_R 0.004f
#define NIGHT_AMBIENT_G 0.005f
#define NIGHT_AMBIENT_B 0.011f
/** Overall scale on the daytime ambient skylight (before the night floor). Tempers the fill-light magnitude so
 *  daytime sits at a natural level; the day/night fade and the warm twilight tint are applied on top. */
#define AMBIENT_SKY_STRENGTH 0.85f
/** Daytime ambient fill colour (LINEAR RGB) — the blue skylight when the sun is well above the horizon. */
#define AMBIENT_DAY_R 0.15f
#define AMBIENT_DAY_G 0.35f
#define AMBIENT_DAY_B 0.75f
/** Twilight ambient fill colour (LINEAR RGB) — the warm sunrise/sunset tint the fill blends toward while the
 *  sun is near the horizon. */
#define AMBIENT_SUNSET_R 0.80f
#define AMBIENT_SUNSET_G 0.28f
#define AMBIENT_SUNSET_B 0.08f
/** How near the horizon (in sun-elevation units, where 0 = on the horizon and 1 = zenith) the ambient warms
 *  toward the sunset colour: the warm tint is a Gaussian of the sun's elevation with this as its width, so a
 *  SMALLER value keeps the fill blue until the sun is closer to setting, a LARGER value warms it earlier.
 *  This is the "orange comes in too early / too late" knob. The warm tint is additionally gated by the daylight
 *  factor so it can never linger once the sun has set. */
#define AMBIENT_SUNSET_BAND 0.15f
/** How much the (directional) sun counts toward the scene-illumination estimate driving eye adaptation. */
#define SUN_ADAPT_WEIGHT 0.5f
/** Base exponential fog density at world+config fog strength 1.0 (per world unit). Distant geometry fades into
 *  the atmospheric sky colour; the world and config fog-strength multipliers scale this. */
#define BASE_FOG_DENSITY 0.008f
// ---- Eye adaptation (HDR auto-exposure) ----
/** Scene illumination that maps to exposure 1.0 (roughly full daylight); brighter dims, darker brightens. */
#define ADAPT_REFERENCE_LUMINANCE 0.80f
/** Exposure clamp: never darker than this (so a bright light cannot black the world out)... */
#define ADAPT_MIN_EXPOSURE 0.5f
/** ===== NIGHT DARKNESS KNOB (2 of 2) =====
 *  ...and never brighter than this. This bounds how far the auto-exposure lifts a dark night, so it is the
 *  dominant control of how bright the night LOOKS: the eye adaptation would otherwise brighten the dim night
 *  ambient all the way back up. LOWER this to make the night DARKER; raise it to brighten the night. (Was 40;
 *  lowered so night reads as night.) Pairs with the NIGHT_AMBIENT_* floor above. */
#define ADAPT_MAX_EXPOSURE 24.0f
/** Floor on the estimated scene luminance, so exposure never divides by ~0. */
#define ADAPT_MIN_LUMINANCE 0.02f
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

/* Caches every uniform location of the PBR shader the renderer sets. */
static void CacheLocations(WorldRendererPbrState* pbr, Shader shader)
{
    pbr->_locViewPos = GetShaderLocation(shader, "viewPos");
    pbr->_locSunDirection = GetShaderLocation(shader, "sunDirection");
    pbr->_locSunColor = GetShaderLocation(shader, "sunColor");
    pbr->_locSunIntensity = GetShaderLocation(shader, "sunIntensity");
    pbr->_locAmbientColor = GetShaderLocation(shader, "ambientColor");
    pbr->_locAmbientIntensity = GetShaderLocation(shader, "ambientIntensity");

    pbr->_locLightVP = GetShaderLocation(shader, "lightVP");
    pbr->_locShadowMap = GetShaderLocation(shader, "shadowMap");
    pbr->_locLightVPFar = GetShaderLocation(shader, "lightVPFar");
    pbr->_locShadowMapFar = GetShaderLocation(shader, "shadowMapFar");
    pbr->_locShadowFarActive = GetShaderLocation(shader, "shadowFarActive");
    pbr->_locShadowStrength = GetShaderLocation(shader, "shadowStrength");
    pbr->_locShadowBias = GetShaderLocation(shader, "shadowBias");
    pbr->_locShadowTexelSize = GetShaderLocation(shader, "shadowTexelSize");

    pbr->_locPointShadowActive = GetShaderLocation(shader, "pointShadowActive");
    pbr->_locPointShadowLightPos = GetShaderLocation(shader, "pointShadowLightPos");
    pbr->_locPointShadowLightRadiance = GetShaderLocation(shader, "pointShadowLightRadiance");
    pbr->_locPointShadowLightRange = GetShaderLocation(shader, "pointShadowLightRange");
    pbr->_locPointShadowCube = GetShaderLocation(shader, "pointShadowCube");
    pbr->_locPointShadowFar = GetShaderLocation(shader, "pointShadowFar");
    pbr->_locPointShadowBias = GetShaderLocation(shader, "pointShadowBias");

    pbr->_locPointLightCount = GetShaderLocation(shader, "pointLightCount");
    pbr->_locPointLightPositions = GetShaderLocation(shader, "pointLightPositions");
    pbr->_locPointLightRadiances = GetShaderLocation(shader, "pointLightRadiances");
    pbr->_locPointLightRanges = GetShaderLocation(shader, "pointLightRanges");

    pbr->_locFogDensity = GetShaderLocation(shader, "fogDensity");
    pbr->_locFogTurbidity = GetShaderLocation(shader, "skyTurbidity");
    pbr->_locFogSkyTint = GetShaderLocation(shader, "skyTint");
    pbr->_locFogSunIntensity = GetShaderLocation(shader, "skySunIntensity");

    // Per-material scalar-factor + map-presence uniforms, uploaded per material by the mesh draw loop.
    pbr->_locMetallic = GetShaderLocation(shader, "metallic");
    pbr->_locRoughness = GetShaderLocation(shader, "roughness");
    pbr->_locAo = GetShaderLocation(shader, "ao");
    pbr->_locEmissiveColor = GetShaderLocation(shader, "emissiveColor");
    pbr->_locEmissiveIntensity = GetShaderLocation(shader, "emissiveIntensity");
    pbr->_locHasMraMap = GetShaderLocation(shader, "hasMraMap");
    pbr->_locHasNormalMap = GetShaderLocation(shader, "hasNormalMap");
    pbr->_locHasEmissiveMap = GetShaderLocation(shader, "hasEmissiveMap");
    pbr->_locAmbientOcclusionMap = GetShaderLocation(shader, "ambientOcclusionMap");
    pbr->_locAoMapActive = GetShaderLocation(shader, "aoMapActive");
    pbr->_locAoResolution = GetShaderLocation(shader, "aoResolution");
}

/* Builds the ambient skylight colour (linear RGB) so it TRACKS the sky's timing, driven by the sun's elevation
 * and the SAME daylight thresholds the sun lighting uses (WorldEnvironment_GetDaylightFactor), so the fill light
 * and the sun agree:
 *   * day  (sun well above the horizon) -> the daytime blue fill, at full brightness;
 *   * twilight (sun near the horizon)   -> blended toward the warm sunrise/sunset colour;
 *   * night (sun below the horizon)     -> the fill fades out to the dim NIGHT_AMBIENT floor.
 * The warm tint is gated by BOTH a narrow band around the horizon (AMBIENT_SUNSET_BAND, a Gaussian of the sun
 * elevation) AND the daylight factor, so it can neither appear while the sun is still high (sky still blue) nor
 * linger once the sun has set — the two bugs the flat sky-colour gradient produced. The hues are authored
 * (AMBIENT_DAY_* / AMBIENT_SUNSET_*), tinted by AmbientSkylightColor (WHITE = neutral) and scaled by
 * AmbientSkylightIntensity x AMBIENT_SKY_STRENGTH; the whole fill fades with the daylight factor. */
static void ComputeAmbientColor(const WorldEnvironment* environment, float sunElevation, float daylight,
    float outRGB[3])
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
 * which the shader shades separately with its shadow. */
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
            .Position = Light->Position,
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

/* Maps an adapted luminance to a tonemap exposure: reference/adapted, clamped. Day-level luminance gives ~1,
 * a dark night gives a large exposure (revealing the dim ambient), a bright light gives a small one. */
static float ExposureFromAdapted(float adapted)
{
    float LuminanceValue = (adapted < ADAPT_MIN_LUMINANCE) ? ADAPT_MIN_LUMINANCE : adapted;
    float Exposure = ADAPT_REFERENCE_LUMINANCE / LuminanceValue;
    if (Exposure < ADAPT_MIN_EXPOSURE) { Exposure = ADAPT_MIN_EXPOSURE; }
    if (Exposure > ADAPT_MAX_EXPOSURE) { Exposure = ADAPT_MAX_EXPOSURE; }
    return Exposure;
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererLighting_LoadShader(WorldRenderer* self)
{
    self->_pbr._shader = WorldRendererCommon_LoadShader(self, PBR_SHADER_ASSET_NAME);
    if (self->_pbr._shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(self->_pbr._shader);
    CacheLocations(&self->_pbr, RayShader);

    // The renderer's own location table: the asset's, with the material map samplers pointed at the PBR
    // shader's sampler names, so DrawMesh binds each material's textures to their conventional units (albedo
    // -> 0 is already raylib's default "texture0"). The METALNESS slot carries the packed ORM map, matching where
    // ModelDefinition stores the mra texture. Patching a copy leaves the shared shader asset's table untouched.
    Memory_Copy(RayShader.locs, self->_pbr._locs, sizeof(self->_pbr._locs));
    self->_pbr._locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(RayShader, "mraMap");
    self->_pbr._locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(RayShader, "normalMap");
    self->_pbr._locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(RayShader, "emissiveMap");
    self->_pbr._passShader = (Shader){ .id = RayShader.id, .locs = self->_pbr._locs };

    // Constant shadow parameters; upload once (material params are per-material).
    float ShadowBias = SHADOW_BIAS;
    float ShadowTexel = 1.0f / (float)SHADOW_MAP_SIZE;
    SetShaderValue(RayShader, self->_pbr._locShadowBias, &ShadowBias, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_pbr._locShadowTexelSize, &ShadowTexel, SHADER_UNIFORM_FLOAT);

    // Point the renderer-owned samplers at their fixed units once (see WorldRendererTextureUnit).
    AssignSamplerUnit(RayShader, self->_pbr._locShadowMap, WorldRendererTextureUnit_ShadowNear);
    AssignSamplerUnit(RayShader, self->_pbr._locShadowMapFar, WorldRendererTextureUnit_ShadowFar);
    AssignSamplerUnit(RayShader, self->_pbr._locPointShadowCube, WorldRendererTextureUnit_PointShadowCube);
    AssignSamplerUnit(RayShader, self->_pbr._locAmbientOcclusionMap, WorldRendererTextureUnit_AmbientOcclusion);

    // The shader's point-light arrays must hold the WORLD_MAX_FORWARD_LIGHTS the culling may select; with fewer,
    // upload only as many as fit (the shader also clamps its loop to its own array size).
    self->_pbr._maxPointLights = CountShaderPointLights(RayShader);
    if ((self->_pbr._maxPointLights < WORLD_MAX_FORWARD_LIGHTS) && (self->_logger != NULL))
    {
        Error LogResult = Logger_LogWarningFormatted(self->_logger,
            (const unsigned char*)u8"WorldRenderer: \"%s\" holds %d point lights, fewer than the %d the renderer "
            u8"selects (WORLD_MAX_FORWARD_LIGHTS); objects get at most %d.",
            (const char*)PBR_SHADER_ASSET_NAME, self->_pbr._maxPointLights, WORLD_MAX_FORWARD_LIGHTS,
            self->_pbr._maxPointLights);
        Error_Deconstruct(&LogResult);
    }
}

void WorldRendererLighting_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame)
{
    const WorldEnvironment* Environment = frame->Environment;

    // Day-night cycle: the sun stops lighting objects once it is below the horizon, and the ambient skylight
    // follows the sky's timing (see ComputeAmbientColor).
    WorldRendererCommon_ColorToLinear(Environment->SunColor, frame->SunColor);
    frame->SunIntensity = Environment->SunIntensity * frame->Daylight;
    ComputeAmbientColor(Environment, frame->SunDirection.y, frame->Daylight, frame->AmbientColor);

    // The base scene-illumination luminance (ambient + weighted sun) eye adaptation starts from; it adds nearby
    // point lights and eases the exposure toward it.
    frame->BaseSceneLuminance = Luminance(frame->AmbientColor)
        + Luminance(frame->SunColor) * frame->SunIntensity * SUN_ADAPT_WEIGHT;

    // Atmospheric distance fog density = base x world x config fog strength.
    float FogDensity = BASE_FOG_DENSITY * Environment->FogStrength * WorldRendererCommon_ConfigFogStrength(self);
    frame->FogDensity = (FogDensity < 0.0f) ? 0.0f : FogDensity;

    // shadowStrength is shared by the sun and point-shadow visibility, so it is gated on global shadow
    // enablement (NOT the sun being up) — otherwise a point light would cast no shadow at night. The sun's own
    // shadow term still self-gates (frustum test + the sun contributes ~0 at night, so a stale sun map cannot
    // show).
    float EffectiveShadowStrength = Environment->ShadowStrength * WorldRendererCommon_ConfigShadowStrength(self);
    frame->ShadowStrength = (Environment->AreShadowsEnabled && (EffectiveShadowStrength > 0.0f))
        ? EffectiveShadowStrength : 0.0f;

    BuildForwardLights(self, frame);
}

void WorldRendererLighting_UploadFrame(WorldRenderer* self, const WorldRenderFrame* frame, bool applyScreenAo)
{
    if (self->_pbr._shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(self->_pbr._shader);
    const WorldEnvironment* Environment = frame->Environment;

    float ViewPos[3] = { frame->Camera->Position.x, frame->Camera->Position.y, frame->Camera->Position.z };
    SetShaderValue(RayShader, self->_pbr._locViewPos, ViewPos, SHADER_UNIFORM_VEC3);
    float SunDir[3] = { frame->SunDirection.x, frame->SunDirection.y, frame->SunDirection.z };
    SetShaderValue(RayShader, self->_pbr._locSunDirection, SunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_pbr._locSunColor, frame->SunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_pbr._locSunIntensity, &frame->SunIntensity, SHADER_UNIFORM_FLOAT);

    // Ambient is folded into the colour (intensity kept at 1).
    SetShaderValue(RayShader, self->_pbr._locAmbientColor, frame->AmbientColor, SHADER_UNIFORM_VEC3);
    float AmbientIntensity = 1.0f;
    SetShaderValue(RayShader, self->_pbr._locAmbientIntensity, &AmbientIntensity, SHADER_UNIFORM_FLOAT);

    // Atmospheric distance fog: the PBR shader fades distant geometry into the SAME sky the sky pass draws,
    // evaluated per fragment along its view ray, so far objects dissolve seamlessly into the sky (and, because
    // the atmospheric sky is already dark at night, distant geometry fades to the dark night sky on its own).
    // Feed it the sky parameters the sky pass uses (turbidity, linear tint, RAW — not day-scaled — sun
    // intensity); the sun direction is the one uploaded above.
    SetShaderValue(RayShader, self->_pbr._locFogTurbidity, &Environment->SkyTurbidity, SHADER_UNIFORM_FLOAT);
    float FogSkyTint[3];
    WorldRendererCommon_ColorToLinear(Environment->SkyTint, FogSkyTint);
    SetShaderValue(RayShader, self->_pbr._locFogSkyTint, FogSkyTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_pbr._locFogSunIntensity, &Environment->SunIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_pbr._locFogDensity, &frame->FogDensity, SHADER_UNIFORM_FLOAT);

    // Sun shadow: upload the light matrices and bind the cascade depth maps to their fixed units (the samplers
    // already point there) when shadows are active this frame. Near cascade always; far only when its map exists.
    if (frame->IsSunShadowActive)
    {
        SetShaderValueMatrix(RayShader, self->_pbr._locLightVP, self->_sunShadow._nearLightVP);
        rlActiveTextureSlot(WorldRendererTextureUnit_ShadowNear);
        rlEnableTexture(self->_sunShadow._nearMap.depth.id);

        int ShadowFarActive = self->_sunShadow._hasFarMap ? 1 : 0;
        SetShaderValue(RayShader, self->_pbr._locShadowFarActive, &ShadowFarActive, SHADER_UNIFORM_INT);
        if (self->_sunShadow._hasFarMap)
        {
            SetShaderValueMatrix(RayShader, self->_pbr._locLightVPFar, self->_sunShadow._farLightVP);
            rlActiveTextureSlot(WorldRendererTextureUnit_ShadowFar);
            rlEnableTexture(self->_sunShadow._farMap.depth.id);
        }
    }

    // Screen-space AO: bind the AO-multiplier map (computed in the pre-pass) so the PBR ambient term is occluded.
    // Bound like the shadow maps (manual slot; the scene pass draws via DrawMesh = a direct draw, not the 2D
    // batch). The crisp overlay passes applyScreenAo=false (its full-res gl_FragCoord would mis-sample this
    // low-res map). aoMapActive false => the shader leaves the ambient term unoccluded.
    int AoMapActive = (applyScreenAo && frame->IsAoActive) ? 1 : 0;
    SetShaderValue(RayShader, self->_pbr._locAoMapActive, &AoMapActive, SHADER_UNIFORM_INT);
    if (AoMapActive)
    {
        float AoResolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
        SetShaderValue(RayShader, self->_pbr._locAoResolution, AoResolution, SHADER_UNIFORM_VEC2);
        rlActiveTextureSlot(WorldRendererTextureUnit_AmbientOcclusion);
        rlEnableTexture(self->_ao._target.texture.id);
    }

    // Point-light (cube) shadow: shade the selected shadow light separately with its cube map. Bind the cube to
    // its own slot. The shader gates its own sampling on pointShadowActive.
    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    bool IsPointShadowActive = frame->IsPointShadowActive && (frame->ShadowLightIndex < LightCount);
    int PointShadowActive = IsPointShadowActive ? 1 : 0;
    SetShaderValue(RayShader, self->_pbr._locPointShadowActive, &PointShadowActive, SHADER_UNIFORM_INT);
    if (IsPointShadowActive)
    {
        const WorldRenderLight* Light = &Lights[frame->ShadowLightIndex];
        float LightPos[3] = { Light->Position.x, Light->Position.y, Light->Position.z };
        SetShaderValue(RayShader, self->_pbr._locPointShadowLightPos, LightPos, SHADER_UNIFORM_VEC3);

        float Radiance[3];
        WorldRendererCommon_ColorToLinear(Light->Color, Radiance);
        for (int Channel = 0; Channel < 3; Channel++)
        {
            Radiance[Channel] *= Light->Intensity;
        }
        SetShaderValue(RayShader, self->_pbr._locPointShadowLightRadiance, Radiance, SHADER_UNIFORM_VEC3);
        SetShaderValue(RayShader, self->_pbr._locPointShadowLightRange, &Light->Range, SHADER_UNIFORM_FLOAT);
        SetShaderValue(RayShader, self->_pbr._locPointShadowFar, &self->_pointShadow._far, SHADER_UNIFORM_FLOAT);
        float PointBias = POINT_SHADOW_BIAS;
        SetShaderValue(RayShader, self->_pbr._locPointShadowBias, &PointBias, SHADER_UNIFORM_FLOAT);

        rlActiveTextureSlot(WorldRendererTextureUnit_PointShadowCube);
        rlEnableTextureCubemap(self->_pointShadow._cube);
    }

    SetShaderValue(RayShader, self->_pbr._locShadowStrength, &frame->ShadowStrength, SHADER_UNIFORM_FLOAT);
}

void WorldRendererLighting_UploadObjectLights(WorldRenderer* self, Vector3 center, float radius)
{
    if ((self->_pbr._shader == NULL) || (self->_pbr._locPointLightCount < 0))
    {
        return;
    }

    const void* CandidateData = self->_pbr._forwardLights._data;
    const void* RadianceData = self->_pbr._forwardRadiances._data;
    const WorldCullingLight* Candidates = CandidateData;
    const WorldRendererRadiance* Radiances = RadianceData;

    size_t Selected[WORLD_MAX_FORWARD_LIGHTS];
    size_t Count = WorldLightCulling_SelectForSphere(Candidates, self->_pbr._forwardLights._count, center, radius,
        Selected, (size_t)self->_pbr._maxPointLights);

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

    Shader RayShader = GameShader_GetRaylibShader(self->_pbr._shader);
    int CountInt = (int)Count;
    SetShaderValue(RayShader, self->_pbr._locPointLightCount, &CountInt, SHADER_UNIFORM_INT);
    if (Count > 0)
    {
        SetShaderValueV(RayShader, self->_pbr._locPointLightPositions, Positions, SHADER_UNIFORM_VEC3, CountInt);
        SetShaderValueV(RayShader, self->_pbr._locPointLightRadiances, RadianceValues, SHADER_UNIFORM_VEC3, CountInt);
        SetShaderValueV(RayShader, self->_pbr._locPointLightRanges, Ranges, SHADER_UNIFORM_FLOAT, CountInt);
    }
}

/* raylib's DrawMesh uploads colDiffuse and binds the material's textures (via the map-sampler locations of the
 * pass shader's table), but NOT the scalar factors or the has*Map guards, so this does that. A map flag is true
 * only when the slot actually holds a texture; the shader must not sample an unbound map sampler (it would read
 * texture unit 0 = albedo — the silent-fallback bug seen across the post passes). */
void WorldRendererLighting_UploadMaterial(WorldRenderer* self, const Material* material)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_pbr._shader);
    float Metallic = material->maps[MATERIAL_MAP_METALNESS].value;
    float Roughness = material->maps[MATERIAL_MAP_ROUGHNESS].value;
    float Ao = material->maps[MATERIAL_MAP_OCCLUSION].value;
    float EmissiveIntensity = material->maps[MATERIAL_MAP_EMISSION].value;
    float EmissiveColor[3];
    WorldRendererCommon_ColorToLinear(material->maps[MATERIAL_MAP_EMISSION].color, EmissiveColor);

    int HasMra = (material->maps[MATERIAL_MAP_METALNESS].texture.id > 0) ? 1 : 0;
    int HasNormal = (material->maps[MATERIAL_MAP_NORMAL].texture.id > 0) ? 1 : 0;
    int HasEmissive = (material->maps[MATERIAL_MAP_EMISSION].texture.id > 0) ? 1 : 0;

    SetShaderValue(RayShader, self->_pbr._locMetallic, &Metallic, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_pbr._locRoughness, &Roughness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_pbr._locAo, &Ao, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_pbr._locEmissiveColor, EmissiveColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, self->_pbr._locEmissiveIntensity, &EmissiveIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_pbr._locHasMraMap, &HasMra, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, self->_pbr._locHasNormalMap, &HasNormal, SHADER_UNIFORM_INT);
    SetShaderValue(RayShader, self->_pbr._locHasEmissiveMap, &HasEmissive, SHADER_UNIFORM_INT);
}

/* Starts from the frame's base scene luminance (ambient + sun), adds nearby point lights (being near a light
 * raises the estimate -> lowers exposure), then eases the adapted luminance toward it. A non-finite estimate (a
 * corrupt light or environment value) holds the adaptation, so it can never be poisoned for good. */
float WorldRendererLighting_UpdateExposure(WorldRenderer* self, const WorldRenderFrame* frame, float deltaSeconds)
{
    float SceneLuminance = frame->BaseSceneLuminance;
    Vector3 CameraPosition = frame->Camera->Position;

    size_t LightCount = 0;
    const WorldRenderLight* Lights = WorldRenderCache_GetLights(&self->_cache, &LightCount);
    for (size_t Index = 0; Index < LightCount; Index++)
    {
        const WorldRenderLight* Light = &Lights[Index];
        if ((Light->Range <= 0.0f) || (Light->Intensity <= 0.0f))
        {
            continue;
        }
        float Distance = Vector3Distance(Light->Position, CameraPosition);
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
        SceneLuminance = self->_exposure._isInitialized ? self->_exposure._adaptedLuminance : ADAPT_REFERENCE_LUMINANCE;
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
    return ExposureFromAdapted(self->_exposure._adaptedLuminance);
}
