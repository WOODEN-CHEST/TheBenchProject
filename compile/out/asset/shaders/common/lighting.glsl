// The world's lighting, shared by the lit shaders (world_pbr and world_sprite), for #include "common/lighting": the
// frame's lighting uniforms (the renderer uploads the same values to every shader that includes this file), the
// Cook-Torrance BRDF, the sky's two directional lights (the sun and the moon) with the cascades' shadow, forward point
// lights and the shadow-casting point light with its cube shadow, the screen-space AO of the ambient term, the eye's
// night vision and the atmospheric distance fog.
//
// Two families of light functions: the plain ones (SkyLight, PointLights) the PBR shader uses, and the wrapped ones
// (SkyLightWrapped, PointLightsWrapped) sprites use, whose diffuse part reaches past the terminator ("wrap lighting",
// for billboards standing in for round things). They are kept apart on purpose: the plain ones compute exactly what
// the PBR shader computed before they moved here, so its image did not change by a single bit.

#include "common/math"
#include "common/atmosphere"
#include "common/night_sky"
#include "common/night_vision"
#include "common/distance_packing"

// Camera and lighting, set by WorldRenderer each frame. Light colours are LINEAR.
uniform vec3 viewPos;          // camera world position
uniform vec3 sunDirection;     // unit direction TO the sun, world space (also the fog sky's sun)
uniform vec3 sunColor;         // linear
uniform float sunIntensity;    // the sun's light: daylight included, 0 at night
uniform vec3 moonDirection;    // unit direction TO the moon, world space (also the fog sky's moon)
uniform vec3 moonColor;        // linear
uniform float moonIntensity;   // the moon's light: phase, height and night included, 0 by day or without a moon
uniform vec3 ambientColor;     // linear
uniform float ambientIntensity;

// Shadow map: TWO cascades, rendered from the sky's shadow light: the sun by day, the moon on bright nights. The NEAR
// cascade is a small, sharp map for close detail; the FAR cascade is a large, coarse map for distant / large casters
// (each object's WorldShadowTier decides which cascade(s) it was rendered into). A fragment samples the near cascade
// first and falls back to the far cascade when it lies beyond the near frustum.
uniform mat4 lightVP;          // world -> near-cascade light-clip space
uniform sampler2D shadowMap;   // near-cascade directional depth map
uniform mat4 lightVPFar;       // world -> far-cascade light-clip space
uniform sampler2D shadowMapFar;// far-cascade directional depth map
uniform bool shadowFarActive;  // whether the far cascade is available this frame
uniform float shadowStrength;  // 0..1, how much a shadowed light is darkened (the cascades and the point shadow)
uniform float shadowBias;      // base depth bias to combat shadow acne
uniform float shadowTexelSize; // 1.0 / shadow-map resolution, for PCF tap offsets
uniform vec3 shadowLightDirection; // unit direction TO the light the cascades were rendered from (for the slope bias)
uniform float sunShadowFade;   // 0..1, how much the cascades shadow the sun: 0 while the moon casts them or neither
uniform float moonShadowFade;  // 0..1, how much they shadow the moon. Both 0: the cascades are not sampled at all

// Screen-space ambient occlusion, computed in a pre-pass (shaders/ao) and applied to ONLY the ambient term (so it never
// dims the direct sun / point lights). aoMapActive false => no SSAO this frame.
uniform sampler2D ambientOcclusionMap; // AO multiplier in R (1 = open, < 1 = occluded)
uniform bool aoMapActive;
uniform vec2 aoResolution;             // scene-target size in pixels, to map gl_FragCoord -> AO map uv

// Point lights, culled + uploaded per object by the renderer (nearest/strongest that reach this object).
// MAX_POINT_LIGHTS should equal WORLD_MAX_FORWARD_LIGHTS in WorldLightCulling.h. A mismatch is safe: the loops below
// clamp to the array size, and the renderer checks the arrays at load and never uploads more than fit.
#define MAX_POINT_LIGHTS 8
uniform int pointLightCount;                          // active entries in the arrays below (0..MAX)
uniform vec3 pointLightPositions[MAX_POINT_LIGHTS];   // world space
uniform vec3 pointLightRadiances[MAX_POINT_LIGHTS];   // linear colour * intensity
uniform float pointLightRanges[MAX_POINT_LIGHTS];     // reach radius (attenuation falls to 0 here)

// ONE shadow-casting point light, shaded SEPARATELY from the culled set above (which excludes it) so it can sample its
// own omnidirectional (cube) shadow map. pointShadowActive false => no shadow-casting point light.
uniform bool pointShadowActive;
uniform vec3 pointShadowLightPos;       // world space
uniform vec3 pointShadowLightRadiance;  // linear colour * intensity
uniform float pointShadowLightRange;    // reach radius
uniform samplerCube pointShadowCube;    // packed (RGBA8) linear distance from the light, per direction
uniform float pointShadowFar;           // far range the stored distance was normalized by
uniform float pointShadowBias;          // world-space depth bias to combat shadow acne

// Atmospheric distance fog: distant geometry fades into the SAME sky the sky pass draws, evaluated along the fragment's
// own view ray, so far objects dissolve seamlessly into the sky (not a flat "close but not quite" colour). The sky pass
// includes shaders/common/atmosphere.glsl too, and the renderer gives the uniforms below the values the sky pass gets.
// fogDensity 0 disables fog.
uniform float fogDensity;
uniform float skyTurbidity;     // atmospheric haze; == WorldEnvironment.SkyTurbidity (same value the sky pass gets)
uniform vec3  skyTint;          // linear sky tint multiplier (same value the sky pass gets)
uniform float skySunIntensity;  // RAW sun intensity (NOT day-scaled); == WorldEnvironment.SunIntensity
uniform vec3  skyglowRadiance;  // light pollution's glow at the horizon, linear (same value the sky pass gets)
uniform float skyMoonIntensity; // the moonlit sky's light relative to the sun's (same value the sky pass gets); 0 = none

// The eye's night vision (shaders/common/night_vision.glsl): dim reflected light loses its colour as it does for the
// eye; emitted light keeps it. The same values the sky pass gets.
uniform float nightVisionFieldLuminance; // the scene luminance the sky's light gives (linear, the game's light units)
uniform float nightVisionHalfLuminance;  // night vision's calibration on the game's light scale (source/NightVision.h)
uniform float nightVisionSteepness;
uniform float nightVisionStrength;       // 0 none (colourful nights) .. 1 the eye
uniform vec3  nightVisionTint;           // the rods' grey, luminance 1 (the world's blue shift)


// ---- The Cook-Torrance BRDF ------------------------------------------------------------------------------------
// GGX / Trowbridge-Reitz normal distribution.
float DistributionGGX(vec3 n, vec3 h, float rough)
{
    float a = rough*rough;
    float a2 = a*a;
    float nDotH = max(dot(n, h), 0.0);
    float nDotH2 = nDotH*nDotH;
    float denom = nDotH2*(a2 - 1.0) + 1.0;
    denom = PI*denom*denom;
    return a2/max(denom, 1e-5);
}

// Schlick-GGX geometry term (direct lighting k).
float GeometrySchlickGGX(float nDotV, float rough)
{
    float r = rough + 1.0;
    float k = (r*r)/8.0;
    return nDotV/(nDotV*(1.0 - k) + k);
}

// Smith's method: geometry for both view and light directions.
float GeometrySmith(vec3 n, vec3 v, vec3 l, float rough)
{
    return GeometrySchlickGGX(max(dot(n, v), 0.0), rough)*GeometrySchlickGGX(max(dot(n, l), 0.0), rough);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0)*pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// One directional light (the sun or the moon) through the Cook-Torrance BRDF: light from direction l (unit, toward the
// light) with the given radiance, times its visibility (1 = unshadowed).
vec3 DirectionalLight(vec3 n, vec3 v, vec3 l, vec3 radiance, float visibility, vec3 albedo, vec3 f0, float rough,
    float metal)
{
    vec3 h = normalize(v + l);
    float ndf = DistributionGGX(n, h, rough);
    float g = GeometrySmith(n, v, l, rough);
    vec3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);

    vec3 numerator = ndf*g*f;
    float denom = 4.0*max(dot(n, v), 0.0)*max(dot(n, l), 0.0) + 1e-4;
    vec3 specular = numerator/denom;

    // Diffuse energy is what is not reflected specularly; metals have no diffuse.
    vec3 kD = (vec3(1.0) - f)*(1.0 - metal);
    float nDotL = max(dot(n, l), 0.0);
    return (kD*albedo/PI + specular)*radiance*nDotL*visibility;
}

// One light through the BRDF with WRAP lighting on its diffuse part: the diffuse factor is (dot(n, l) + wrap) / (1 +
// wrap) instead of dot(n, l), so the light reaches past the terminator. At wrap 1 it is dot(n, l)*0.5 + 0.5: lit from
// the side, half lit; lit from straight behind, dark. A billboard stands in for a round thing (a tree, a person) seen
// from the front, whose sides would catch a light from the side. The specular part keeps the plain dot(n, l); wrap 0 is
// the plain BRDF. Returns the light without its visibility or falloff.
vec3 WrappedLight(vec3 n, vec3 v, vec3 l, vec3 radiance, float wrap, vec3 albedo, vec3 f0, float rough, float metal)
{
    vec3 h = normalize(v + l);
    float ndf = DistributionGGX(n, h, rough);
    float g = GeometrySmith(n, v, l, rough);
    vec3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);
    float nDotL = max(dot(n, l), 0.0);
    vec3 specular = (ndf*g*f)/(4.0*max(dot(n, v), 0.0)*nDotL + 1e-4);
    vec3 kD = (vec3(1.0) - f)*(1.0 - metal);
    float diffuseNDotL = max((dot(n, l) + wrap)/(1.0 + wrap), 0.0);
    return (kD*albedo/PI*diffuseNDotL + specular*nDotL)*radiance;
}


// ---- The cascades' shadow --------------------------------------------------------------------------------------
// The far cascade's texels cover ~5x more world than the near cascade's, so it needs a proportionally larger depth bias
// to avoid self-shadow acne; the near cascade uses the base bias.
const float SHADOW_FAR_BIAS_SCALE = 4.0;

// Samples ONE shadow cascade. Returns -1.0 when the fragment falls outside this cascade's frustum (so the caller can
// fall back to the next cascade / treat it as lit), else 0 (fully lit) .. 1 (fully shadowed). Crisp pixel-art shadow: a
// 2x2 PCF (the minimal filtering that stops a single-tap edge crawling pixel-to-pixel as the camera moves, while staying
// sharp at the low resolution) with a slope-scaled bias to combat acne.
float SampleShadowCascade(sampler2D map, mat4 vp, vec3 worldPos, float nDotL, float biasScale)
{
    vec4 clip = vp*vec4(worldPos, 1.0);
    vec3 proj = clip.xyz/clip.w;
    proj = proj*0.5 + 0.5; // NDC -> [0,1]
    if ((proj.z > 1.0) || (proj.x < 0.0) || (proj.x > 1.0) || (proj.y < 0.0) || (proj.y > 1.0))
    {
        return -1.0; // outside this cascade
    }

    float bias = max(shadowBias*biasScale*(1.0 - nDotL), shadowBias*biasScale*0.15);
    // 2x2 PCF: four taps half a texel off-centre, averaged. Reads as a hard edge after the point-upscale but dithers the
    // boundary just enough to stop sub-pixel shimmer.
    float shadow = 0.0;
    shadow += (proj.z - bias > texture(map, proj.xy + vec2(-0.5, -0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(map, proj.xy + vec2( 0.5, -0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(map, proj.xy + vec2(-0.5,  0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - bias > texture(map, proj.xy + vec2( 0.5,  0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    return shadow*0.25;
}

// Returns 0 (lit) .. 1 (shadowed) for the fragment, sampling the NEAR cascade first (sharp) and falling back to the FAR
// cascade (coarse, long-range) when the fragment lies beyond the near frustum. Outside both = lit. Samples nothing while
// the cascades shadow neither light (they did not run this frame).
float ComputeShadow(vec3 worldPos, float nDotL)
{
    if ((shadowStrength <= 0.0) || ((sunShadowFade <= 0.0) && (moonShadowFade <= 0.0)))
    {
        return 0.0;
    }
    float shadow = SampleShadowCascade(shadowMap, lightVP, worldPos, nDotL, 1.0);
    if ((shadow < 0.0) && shadowFarActive)
    {
        shadow = SampleShadowCascade(shadowMapFar, lightVPFar, worldPos, nDotL, SHADOW_FAR_BIAS_SCALE);
    }
    return max(shadow, 0.0);
}

// How much the cascades darken the light that cast them at a point (0..1): their shadow times the shadow strength.
// shadowPos is where the shadow map is read: the fragment's own position, or for a billboard the matching point of the
// sprite as the light sees it (its shadow-map entry faces the light). nDotL (toward the shadow light) sets the slope
// bias.
float SkyShadowAmount(vec3 shadowPos, float nDotL)
{
    float shadow = ComputeShadow(shadowPos, nDotL);
    return shadow*clamp(shadowStrength, 0.0, 1.0);
}

// The sky's two lights through the plain BRDF: the sun, then (at night) the moon. The cascades' shadow darkens the
// direct term of the light that cast it (by its fade), never the ambient.
vec3 SkyLight(vec3 n, vec3 v, float shadowAmount, vec3 albedo, vec3 f0, float rough, float metal)
{
    vec3 direct = DirectionalLight(n, v, normalize(sunDirection), sunColor*sunIntensity,
        1.0 - shadowAmount*sunShadowFade, albedo, f0, rough, metal);
    if (moonIntensity > 0.0)
    {
        direct += DirectionalLight(n, v, normalize(moonDirection), moonColor*moonIntensity,
            1.0 - shadowAmount*moonShadowFade, albedo, f0, rough, metal);
    }
    return direct;
}

// The sky's two lights with wrap lighting (WrappedLight), shadowed like SkyLight.
vec3 SkyLightWrapped(vec3 n, vec3 v, float shadowAmount, float wrap, vec3 albedo, vec3 f0, float rough, float metal)
{
    vec3 direct = WrappedLight(n, v, normalize(sunDirection), sunColor*sunIntensity, wrap, albedo, f0, rough, metal)
        *(1.0 - shadowAmount*sunShadowFade);
    if (moonIntensity > 0.0)
    {
        direct += WrappedLight(n, v, normalize(moonDirection), moonColor*moonIntensity, wrap, albedo, f0, rough, metal)
            *(1.0 - shadowAmount*moonShadowFade);
    }
    return direct;
}


// ---- Point lights ----------------------------------------------------------------------------------------------
// 20 sample directions for cube-map PCF (a small 3D disk around the light->fragment ray).
const vec3 CUBE_PCF_OFFSETS[20] = vec3[]
(
    vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1),
    vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
    vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
    vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
    vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

// 0 (lit) .. 1 (shadowed) for the shadow-casting point light. Samples the light's cube map along the light->fragment
// direction with soft PCF, comparing the stored nearest-occluder distance to this fragment's distance from the light.
// The bias is SLOPE-SCALED (much larger where the light grazes the surface): a constant bias leaves grazing-angle
// self-shadow acne (the streaky bands). PCF also blends the cube's face seams so they do not show as a hard cut through
// the shadow.
float ComputePointShadow(vec3 fragPos, vec3 nrm)
{
    vec3 toFrag = fragPos - pointShadowLightPos;
    float current = length(toFrag);
    vec3 toLight = (current > 1e-4) ? (-toFrag/current) : vec3(0.0, 1.0, 0.0);
    float ndotl = max(dot(normalize(nrm), toLight), 0.0);

    // Slope-scaled world-space bias: full at grazing incidence, a small floor head-on.
    float bias = max(pointShadowBias*(1.0 - ndotl), pointShadowBias*0.2);

    // PCF disk widens a little with distance (the cube texel covers more world far from the light).
    float diskRadius = 0.02 + 0.02*current;
    float shadow = 0.0;
    for (int i = 0; i < 20; i++)
    {
        float stored = unpackDistance(texture(pointShadowCube, toFrag + CUBE_PCF_OFFSETS[i]*diskRadius))*pointShadowFar;
        if ((current - bias) > stored) { shadow += 1.0; }
    }
    return shadow/20.0;
}

// One point light through the plain BRDF with a physical 1/d^2 falloff, smoothly windowed to exactly 0 at the light's
// range so the shaded set agrees with what the CPU reach-culler decided affects this object (no hard popping at the
// edge).
vec3 PointLight(vec3 n, vec3 v, vec3 worldPos, vec3 lightPos, vec3 radiance, float lightRange, vec3 albedo, vec3 f0,
    float rough, float metal)
{
    vec3 toLight = lightPos - worldPos;
    float dist = length(toLight);
    vec3 lp = toLight/max(dist, 1e-4);
    vec3 hp = normalize(v + lp);

    float ndfp = DistributionGGX(n, hp, rough);
    float gp = GeometrySmith(n, v, lp, rough);
    vec3 fp = FresnelSchlick(max(dot(hp, v), 0.0), f0);
    vec3 specp = (ndfp*gp*fp)/(4.0*max(dot(n, v), 0.0)*max(dot(n, lp), 0.0) + 1e-4);
    vec3 kDp = (vec3(1.0) - fp)*(1.0 - metal);
    float ndlp = max(dot(n, lp), 0.0);

    float range = max(lightRange, 1e-3);
    float window = clamp(1.0 - pow(dist/range, 4.0), 0.0, 1.0);
    float atten = (window*window)/(dist*dist + 1.0);

    return (kDp*albedo/PI + specp)*radiance*ndlp*atten;
}

// One point light with wrap lighting (WrappedLight) and PointLight's falloff.
vec3 PointLightWrapped(vec3 n, vec3 v, vec3 worldPos, vec3 lightPos, vec3 radiance, float lightRange, float wrap,
    vec3 albedo, vec3 f0, float rough, float metal)
{
    vec3 toLight = lightPos - worldPos;
    float dist = length(toLight);
    vec3 lp = toLight/max(dist, 1e-4);
    float range = max(lightRange, 1e-3);
    float window = clamp(1.0 - pow(dist/range, 4.0), 0.0, 1.0);
    float atten = (window*window)/(dist*dist + 1.0);
    return WrappedLight(n, v, lp, radiance, wrap, albedo, f0, rough, metal)*atten;
}

// The point lights through the plain BRDF: the reach-culled set the renderer chose for this object (the count is
// clamped to the arrays' size so a bad upload can never index past them), then the shadow-casting point light, darkened
// by its own cube shadow (shaded here rather than in the culled loop so it can sample the cube; the renderer excludes it
// from that loop). The cube is read at shadowPos with shadowNormal's slope bias (the fragment's own, for models).
vec3 PointLights(vec3 n, vec3 v, vec3 worldPos, vec3 shadowPos, vec3 shadowNormal, vec3 albedo, vec3 f0, float rough,
    float metal)
{
    vec3 pointTotal = vec3(0.0);
    int pointLightLimit = min(pointLightCount, MAX_POINT_LIGHTS);
    for (int i = 0; i < pointLightLimit; i++)
    {
        pointTotal += PointLight(n, v, worldPos, pointLightPositions[i], pointLightRadiances[i], pointLightRanges[i],
            albedo, f0, rough, metal);
    }
    if (pointShadowActive)
    {
        float shadow = ComputePointShadow(shadowPos, shadowNormal);
        float visibility = 1.0 - shadow*clamp(shadowStrength, 0.0, 1.0);
        pointTotal += PointLight(n, v, worldPos, pointShadowLightPos, pointShadowLightRadiance, pointShadowLightRange,
            albedo, f0, rough, metal)*visibility;
    }
    return pointTotal;
}

// The point lights with wrap lighting (PointLightWrapped), shadowed like PointLights.
vec3 PointLightsWrapped(vec3 n, vec3 v, vec3 worldPos, vec3 shadowPos, vec3 shadowNormal, float wrap, vec3 albedo,
    vec3 f0, float rough, float metal)
{
    vec3 pointTotal = vec3(0.0);
    int pointLightLimit = min(pointLightCount, MAX_POINT_LIGHTS);
    for (int i = 0; i < pointLightLimit; i++)
    {
        pointTotal += PointLightWrapped(n, v, worldPos, pointLightPositions[i], pointLightRadiances[i],
            pointLightRanges[i], wrap, albedo, f0, rough, metal);
    }
    if (pointShadowActive)
    {
        float shadow = ComputePointShadow(shadowPos, shadowNormal);
        float visibility = 1.0 - shadow*clamp(shadowStrength, 0.0, 1.0);
        pointTotal += PointLightWrapped(n, v, worldPos, pointShadowLightPos, pointShadowLightRadiance,
            pointShadowLightRange, wrap, albedo, f0, rough, metal)*visibility;
    }
    return pointTotal;
}


// ---- Ambient, night vision and fog -----------------------------------------------------------------------------
// The screen-space AO multiplier at this pixel (1 = open), for the ambient term only; 1 while no AO runs. Only meaningful
// for a surface the AO pre-pass saw (a model, a cutout sprite) or one lying on it (a decal).
float ScreenAo()
{
    return aoMapActive ? texture(ambientOcclusionMap, gl_FragCoord.xy/aoResolution).r : 1.0;
}

// What the eye sees of reflected light (its colour fades at night, a lamp's bright pool keeps it; by day nothing
// changes). Emitted light is added after it and keeps its colour, like the sky's discs.
vec3 NightVisionOf(vec3 color)
{
    return nightVision(color, nightVisionFieldLuminance, nightVisionHalfLuminance, nightVisionSteepness,
        nightVisionStrength, nightVisionTint);
}

// The sky colour along a world-space view ray (the sky pass's result minus the sun and moon discs, the Milky Way and the
// stars, but with the moonlit sky and light pollution's skyglow). This is the colour distant fogged geometry fades
// toward, so it matches the visible sky in that direction.
vec3 fogSkyColor(vec3 rayDir)
{
    vec3 rayOrigin = vec3(0.0, ATMOSPHERE_R_PLANET + 1.0, 0.0);
    vec3 sunDir = normalize(sunDirection);
    float kMie = ATMOSPHERE_K_MIE_BASE*max(skyTurbidity, 0.0)/3.0;
    float iSun = ATMOSPHERE_SUN_RADIANCE*max(skySunIntensity, 0.0);

    // Mirror the ray into the upper hemisphere and darken below the horizon, exactly like the sky pass's main.
    vec3 skyRay = vec3(rayDir.x, abs(rayDir.y), rayDir.z);
    vec3 sky = atmosphere(skyRay, rayOrigin, sunDir, iSun, kMie);
    if (skyMoonIntensity > 0.0)
    {
        sky += atmosphere(skyRay, rayOrigin, normalize(moonDirection), ATMOSPHERE_SUN_RADIANCE*skyMoonIntensity, kMie);
    }
    sky += nightSkySkyglow(skyRay.y, skyglowRadiance);
    float belowFactor = smoothstep(0.0, -0.12, rayDir.y);
    sky *= mix(1.0, 0.5, belowFactor);
    sky *= max(skyTint, vec3(0.0));
    return sky;
}

// How much of the view toward a point the fog takes (0 none .. 1 all), with an exponential falloff; 0 without fog.
float FogFactor(vec3 worldPos)
{
    if (fogDensity <= 0.0)
    {
        return 0.0;
    }
    float fragDistance = length(worldPos - viewPos);
    return clamp(1.0 - exp(-fragDistance*fogDensity), 0.0, 1.0);
}

// Distance fog: fades a colour toward the atmospheric sky colour along the point's own view ray, so distant geometry
// dissolves into the exact sky behind it. The sky is only evaluated when the fog actually contributes (near fragments
// skip the raymarch), and goes through the night vision as the sky pass's does.
vec3 ApplyFog(vec3 color, vec3 worldPos)
{
    float fogFactor = FogFactor(worldPos);
    if (fogFactor > 0.001)
    {
        vec3 viewRay = normalize(worldPos - viewPos);
        vec3 fogSky = NightVisionOf(fogSkyColor(viewRay));
        color = mix(color, fogSky, fogFactor);
    }
    return color;
}
