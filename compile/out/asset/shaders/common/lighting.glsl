// The world's lighting, shared by the lit shaders (world_pbr and world_sprite), for #include "common/lighting": the
// frame's lighting uniforms (the renderer uploads the same values to every shader that includes this file), the
// Cook-Torrance BRDF, the sky's two directional lights (the sun and the moon) with the cascades' shadow, forward point
// lights and the shadow-casting point light with its cube shadow, the sky's ambient light with the screen-space AO and
// interior lighting (common/interior: what roofs keep out, rooms' own light), the eye's night vision and the haze in
// front of every surface (the aerial perspective and the ground haze, common/haze).
//
// THE SKY'S LIGHT (one light scale): the sun and the moon reach a surface through the air (their light above the air
// times the transmittance toward them at the surface's own height: reddened low over the horizon, gone once the disc has
// set there), and its ambient is the sky's own light, the sky's irradiance from above and the ground's bounce from below
// (AmbientLight), plus the moonless night's own light. The atmosphere pass writes that light by height into the haze
// volume's last row every frame (SkyLightAt reads it at the fragment's height); without it (the atmosphere's tables off
// or unavailable) the uniforms hold the light at the camera, the C mirror's.
//
// Two families of light functions: the plain ones (SkyLight, PointLights) the PBR shader uses, and the wrapped ones
// (SkyLightWrapped, PointLightsWrapped) sprites use, whose diffuse part reaches past the terminator ("wrap lighting",
// for billboards standing in for round things). They are kept apart on purpose: the plain ones are exactly the PBR
// shader's BRDF, untouched by the wrap.
//
// RENDER SPACE. "World space" here, and in every shader that includes this file, means render space: the world's
// orientation with the camera at (0, 0, 0). Fragment positions, light positions and the shadow matrices are relative to
// the camera (the renderer subtracts its position in double), so the camera needs no uniform: the view vector is
// -worldPos, the haze's distance length(worldPos). A pattern anchored to the world must not use these positions alone:
// it gets the camera's position wrapped by the pattern's period, in double, from the renderer (agents/WorldRuntime.md,
// "Render space").

#include "common/math"
#include "common/atmosphere"
#include "common/haze"
#include "common/interior"
#include "common/night_vision"
#include "common/distance_packing"

// Lighting, set by WorldRenderer each frame. Light colours are LINEAR.
uniform vec3 sunDirection;     // unit direction TO the sun, world space (also the ground haze's sun)
uniform vec3 moonDirection;    // unit direction TO the moon, world space (also the ground haze's moon)
// The sky's light. While skyLightPerHeight is on, the light at each fragment's height comes from the volume's last
// row; otherwise these four hold it at the camera for every fragment (the light on a surface facing the sun, facing the
// moon, the sky's irradiance on one facing up, the ground's bounce on one facing down).
uniform bool skyLightPerHeight;
uniform vec3 sunLight;
uniform vec3 moonLight;
uniform vec3 skyIrradiance;
uniform vec3 groundIrradiance;
uniform bool isMoonLit;        // false: the moon lights nothing this frame (its terms are skipped)
uniform vec3 ambientTint;      // the ambient's tint and strength on the sky's light (ambient_skylight_color x intensity)
uniform vec3 nightLight;       // the moonless night's own light on a surface, per unit of its albedo

// Shadow map: TWO cascades, rendered from the sky's shadow light: the sun by day, the moon on bright nights. The NEAR
// cascade is a small, sharp map for close detail; the FAR cascade is a large, coarse map for distant / large casters
// (each object's WorldShadowTier decides which cascade(s) it was rendered into). A fragment samples the near cascade
// first and falls back to the far cascade when it lies beyond the near frustum.
uniform mat4 lightVP;          // render space -> near-cascade light-clip space
uniform sampler2D shadowMap;   // near-cascade directional depth map
uniform mat4 lightVPFar;       // render space -> far-cascade light-clip space
uniform sampler2D shadowMapFar;// far-cascade directional depth map
uniform bool shadowFarActive;  // whether the far cascade is available this frame
uniform float shadowStrength;  // 0..1, how much a shadowed light is darkened (the cascades and the point shadow)
uniform vec2 shadowDepthBias;  // the depth bias in the near and the far cascade, in their depth units (half a texel)
uniform vec2 shadowNormalOffset; // how far a lookup moves off its surface at grazing incidence, near and far, metres
uniform float shadowTexelSize; // 1.0 / shadow-map resolution, for PCF tap offsets
uniform vec3 shadowLightDirection; // unit direction TO the light the cascades were rendered from
uniform float sunShadowFade;   // 0..1, how much the cascades shadow the sun: 0 while the moon casts them or neither
uniform float moonShadowFade;  // 0..1, how much they shadow the moon. Both 0: the cascades are not sampled at all

// Screen-space ambient occlusion, computed in a pre-pass (shaders/ao) and applied to ONLY the ambient term (so it never
// dims the direct sun / point lights). aoMapActive false => no SSAO this frame.
uniform sampler2D ambientOcclusionMap; // the ambient pre-pass (shaders/ao): R the AO multiplier (1 = open, < 1 = occluded), G
                                       // the sky's visibility, B the contact light
uniform bool aoMapActive;
uniform vec2 aoResolution;             // scene-target size in pixels, to map gl_FragCoord -> AO map uv
uniform bool skyVisibilityOnScreen;    // the pre-pass's G holds the sky's visibility this frame (else each surface reads it)
uniform bool drawInPrepass;            // per draw: the pre-pass saw this draw's surface at its pixels (false: it faded out of
                                       // the G-buffer, and the pixels hold what lies behind it)

// Point lights, culled + uploaded per object by the renderer (nearest/strongest that reach this object).
// MAX_POINT_LIGHTS should equal WORLD_MAX_FORWARD_LIGHTS in WorldLightCulling.h. A mismatch is safe: the loops below
// clamp to the array size, and the renderer checks the arrays at load and never uploads more than fit.
#define MAX_POINT_LIGHTS 8
uniform int pointLightCount;                          // active entries in the arrays below (0..MAX)
uniform vec3 pointLightPositions[MAX_POINT_LIGHTS];   // render space
uniform vec3 pointLightRadiances[MAX_POINT_LIGHTS];   // linear colour * intensity
uniform float pointLightRanges[MAX_POINT_LIGHTS];     // reach radius (attenuation falls to 0 here)

// ONE shadow-casting point light, shaded SEPARATELY from the culled set above (which excludes it) so it can sample its
// own omnidirectional (cube) shadow map. pointShadowActive false => no shadow-casting point light.
uniform bool pointShadowActive;
uniform vec3 pointShadowLightPos;       // render space
uniform vec3 pointShadowLightRadiance;  // linear colour * intensity
uniform float pointShadowLightRange;    // reach radius
uniform samplerCube pointShadowCube;    // packed (RGBA8) linear distance from the light, per direction
uniform float pointShadowFar;           // far range the stored distance was normalized by
uniform float pointShadowBias;          // world-space depth bias to combat shadow acne

// The haze in front of a surface (common/haze): the air's from the aerial-perspective volume, and the ground haze.
// The volume's screen directions are the scene camera's: a point's place among them is its position through the scene's
// view-projection (the crisp pass, at the window's resolution, reads the same directions). A draw's own haze multiplies
// the frame's: its placement's `haze` (1 the real air's, 0 none: a sign that must read from far away), set before every
// lit draw. The part of the line of sight inside the draw's ambient zones is hazed by their own haze
// (InteriorHazeDistance: a room's still air has none).
uniform mat4 hazeViewProjection;
uniform float hazeMultiplier;

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
// The cascades hold the casters' faces turned away from the light (their front faces are culled), so a lit surface never
// meets itself in the map. A lookup moves off its surface along its geometric normal, the more the more the surface is
// turned from the light (no surface shadows itself through the map's texels at a slant), and keeps only a small depth
// bias: a shadow then reaches the foot of what casts it, and a roof's shadow the top of the wall under it.

// Samples ONE shadow cascade. Returns -1.0 when the fragment falls outside this cascade's frustum (so the caller can
// fall back to the next cascade / treat it as lit), else 0 (fully lit) .. 1 (fully shadowed). Crisp pixel-art shadow: a
// 2x2 PCF (the minimal filtering that stops a single-tap edge crawling pixel-to-pixel as the camera moves, while staying
// sharp at the low resolution). normal is the surface's geometric normal (unit), nDotL its cosine toward the shadow
// light (clamped at 0).
float SampleShadowCascade(sampler2D map, mat4 vp, vec3 worldPos, vec3 normal, float nDotL, float depthBias,
    float normalOffset)
{
    float slant = sqrt(max(1.0 - nDotL*nDotL, 0.0));
    vec4 clip = vp*vec4(worldPos + normal*(normalOffset*slant), 1.0);
    vec3 proj = clip.xyz/clip.w;
    proj = proj*0.5 + 0.5; // NDC -> [0,1]
    if ((proj.z > 1.0) || (proj.x < 0.0) || (proj.x > 1.0) || (proj.y < 0.0) || (proj.y > 1.0))
    {
        return -1.0; // outside this cascade
    }

    // 2x2 PCF: four taps half a texel off-centre, averaged. Reads as a hard edge after the point-upscale but dithers the
    // boundary just enough to stop sub-pixel shimmer.
    float shadow = 0.0;
    shadow += (proj.z - depthBias > texture(map, proj.xy + vec2(-0.5, -0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - depthBias > texture(map, proj.xy + vec2( 0.5, -0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - depthBias > texture(map, proj.xy + vec2(-0.5,  0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    shadow += (proj.z - depthBias > texture(map, proj.xy + vec2( 0.5,  0.5)*shadowTexelSize).r) ? 1.0 : 0.0;
    return shadow*0.25;
}

// Returns 0 (lit) .. 1 (shadowed) for the fragment, sampling the NEAR cascade first (sharp) and falling back to the FAR
// cascade (coarse, long-range) when the fragment lies beyond the near frustum. Outside both = lit. Samples nothing while
// the cascades shadow neither light (they did not run this frame).
float ComputeShadow(vec3 worldPos, vec3 normal, float nDotL)
{
    if ((shadowStrength <= 0.0) || ((sunShadowFade <= 0.0) && (moonShadowFade <= 0.0)))
    {
        return 0.0;
    }
    float shadow = SampleShadowCascade(shadowMap, lightVP, worldPos, normal, nDotL, shadowDepthBias.x,
        shadowNormalOffset.x);
    if ((shadow < 0.0) && shadowFarActive)
    {
        shadow = SampleShadowCascade(shadowMapFar, lightVPFar, worldPos, normal, nDotL, shadowDepthBias.y,
            shadowNormalOffset.y);
    }
    return max(shadow, 0.0);
}

// How much the cascades darken the light that cast them at a point (0..1): their shadow times the shadow strength.
// shadowPos is where the shadow map is read: the fragment's own position, or for a billboard the matching point of the
// sprite as the light sees it (its shadow-map entry faces the light); shadowNormal the geometric normal there (unit), and
// nDotL its cosine toward the shadow light (clamped at 0), which set how far the lookup moves off the surface.
float SkyShadowAmount(vec3 shadowPos, vec3 shadowNormal, float nDotL)
{
    float shadow = ComputeShadow(shadowPos, shadowNormal, nDotL);
    return shadow*clamp(shadowStrength, 0.0, 1.0);
}

// A triangle's own normal at this fragment, turned to the side of `side` (its interpolated normal), from its position's
// screen derivatives: exact across the triangle, whatever its vertices' normals. Call it in uniform control flow.
vec3 FaceNormal(vec3 worldPos, vec3 side)
{
    vec3 face = cross(dFdx(worldPos), dFdy(worldPos));
    float lengthSquared = dot(face, face);
    if (!(lengthSquared > 0.0))
    {
        return side;
    }
    face *= inversesqrt(lengthSquared);
    return (dot(face, side) < 0.0) ? -face : face;
}

// The ambient pre-pass's contact shadow at this pixel (shaders/ao: 1 in one, 0 lit), of the light the cascades were
// rendered from; 0 while no pre-pass ran or it did not see this draw's surface (drawInPrepass).
float ScreenContactShadow()
{
    return (aoMapActive && drawInPrepass) ? 1.0 - texture(ambientOcclusionMap, gl_FragCoord.xy/aoResolution).b : 0.0;
}

// How much the cascades darken the light that cast them on a model's or the terrain's surface (0..1), read off the
// triangle's own face (FaceNormal): the maps hold the faces turned away from the light, so such a face lies in its own
// shadow and takes all of it, and any other is read off its face. An interpolated or normal-mapped normal can lean
// toward the light where its face leans away, and read the face's own entry in the map as lit: specks and bands of light
// inside shade. (So a rounded low-poly shape's terminator follows its faces.) The screen's contact shadow darkens it too,
// where the cascades' texels leave a seam at the foot of what casts a shadow. Nothing while the cascades shadow neither
// light.
float FaceShadowAmount(vec3 worldPos, vec3 faceNormal)
{
    if ((shadowStrength <= 0.0) || ((sunShadowFade <= 0.0) && (moonShadowFade <= 0.0)))
    {
        return 0.0;
    }
    float strength = clamp(shadowStrength, 0.0, 1.0);
    float faceDotL = dot(faceNormal, normalize(shadowLightDirection));
    if (faceDotL <= 0.0)
    {
        return strength;
    }
    return max(SkyShadowAmount(worldPos, faceNormal, faceDotL), ScreenContactShadow()*strength);
}

// ---- The sky's light at a point --------------------------------------------------------------------------------
// What lights a surface at a point: the sun's light on a surface facing it, the moon's, the sky's irradiance on a surface
// facing up and the ground's bounce on one facing down.
struct SkyLightValues
{
    vec3 sun;
    vec3 moon;
    vec3 sky;
    vec3 ground;
};

// One value of the sky's light at a height from the volume's last row, linear between the two heights around it.
vec3 SkyLightValueAt(int value, float coordinate)
{
    int lower = int(floor(coordinate));
    float share = coordinate - float(lower);
    vec3 a = texelFetch(atmosphereVolume, atmosphereSkyLightTexel(value, lower), 0).rgb;
    vec3 b = texelFetch(atmosphereVolume, atmosphereSkyLightTexel(value, lower + 1), 0).rgb;
    return mix(a, b, share);
}

// The sky's light at a render-space point: at its own height above the atmosphere's ground (the camera's plus its height
// in render space), or the camera's while the volume holds none.
SkyLightValues SkyLightAt(vec3 worldPos)
{
    SkyLightValues light;
    if (skyLightPerHeight)
    {
        float coordinate = atmosphereSkyLightCoordinate(atmosphereViewHeight + worldPos.y);
        light.sun = SkyLightValueAt(ATMOSPHERE_SKY_LIGHT_SUN, coordinate);
        light.moon = isMoonLit ? SkyLightValueAt(ATMOSPHERE_SKY_LIGHT_MOON, coordinate) : vec3(0.0);
        light.sky = SkyLightValueAt(ATMOSPHERE_SKY_LIGHT_SKY, coordinate);
        light.ground = SkyLightValueAt(ATMOSPHERE_SKY_LIGHT_GROUND, coordinate);
        return light;
    }
    light.sun = sunLight;
    light.moon = moonLight;
    light.sky = skyIrradiance;
    light.ground = groundIrradiance;
    return light;
}

// The sky's two lights through the plain BRDF: the sun, then (at night) the moon. The cascades' shadow darkens the
// direct term of the light that cast it (by its fade), never the ambient.
vec3 SkyLight(SkyLightValues light, vec3 n, vec3 v, float shadowAmount, vec3 albedo, vec3 f0, float rough, float metal)
{
    vec3 direct = DirectionalLight(n, v, normalize(sunDirection), light.sun, 1.0 - shadowAmount*sunShadowFade, albedo,
        f0, rough, metal);
    if (isMoonLit)
    {
        direct += DirectionalLight(n, v, normalize(moonDirection), light.moon, 1.0 - shadowAmount*moonShadowFade, albedo,
            f0, rough, metal);
    }
    return direct;
}

// The sky's two lights with wrap lighting (WrappedLight), shadowed like SkyLight.
vec3 SkyLightWrapped(SkyLightValues light, vec3 n, vec3 v, float shadowAmount, float wrap, vec3 albedo, vec3 f0,
    float rough, float metal)
{
    vec3 direct = WrappedLight(n, v, normalize(sunDirection), light.sun, wrap, albedo, f0, rough, metal)
        *(1.0 - shadowAmount*sunShadowFade);
    if (isMoonLit)
    {
        direct += WrappedLight(n, v, normalize(moonDirection), light.moon, wrap, albedo, f0, rough, metal)
            *(1.0 - shadowAmount*moonShadowFade);
    }
    return direct;
}

// The ambient light on a surface with the normal n at the point worldPos, per unit of its albedo (times its AO by the
// caller): the sky's irradiance over the upper half of its view and the ground's bounce over the lower half, as a
// Lambertian surface takes them ((sky (1 + n.y) / 2 + ground (1 - n.y) / 2) / pi), tinted, plus the moonless night's own
// light; under a roof all three times the share of the sky the surface sees at its visibility skyVisibility
// (common/interior; SkyVisibilityAt for a model or the terrain) and the zones' sky scale, plus the zones' own light (its
// geometric normal surfaceNormal places it in them).
vec3 AmbientLight(SkyLightValues light, vec3 n, vec3 worldPos, vec3 surfaceNormal, float skyVisibility)
{
    float up = clamp(n.y, -1.0, 1.0);
    vec3 skyAmbient = (light.sky*(1.0 + up) + light.ground*(1.0 - up))*(0.5/PI)*ambientTint + nightLight;
    if (!interiorActive)
    {
        return skyAmbient;
    }
    InteriorZoneLight zones = InteriorZonesAt(worldPos, surfaceNormal);
    float share = InteriorSkyShare(skyVisibility)*zones.skyScale;
    return skyAmbient*share + zones.ambient;
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


// ---- Ambient, night vision and haze ----------------------------------------------------------------------------
// The ambient pre-pass's sky visibility at this pixel, blurred over its pattern (common/interior): for the surface the
// pre-pass saw there.
float ScreenSkyVisibility()
{
    return texture(ambientOcclusionMap, gl_FragCoord.xy/aoResolution).g;
}

// The sky's visibility of a model's or the terrain's surface at this pixel: the pre-pass's while it holds this frame's and
// saw this draw's surface there; otherwise only what lies straight above the point p with the geometric normal n
// (InteriorSkyOverhead: the crisp overlay, a frame without the pre-pass, a draw faded out of the G-buffer).
float SkyVisibilityAt(vec3 p, vec3 n)
{
    return (aoMapActive && skyVisibilityOnScreen && drawInPrepass) ? ScreenSkyVisibility() : InteriorSkyOverhead(p, n);
}

// The screen-space AO multiplier at this pixel (1 = open), for the ambient term only; 1 while no AO runs or the pre-pass
// did not see this draw's surface (drawInPrepass).
float ScreenAo()
{
    return (aoMapActive && drawInPrepass) ? texture(ambientOcclusionMap, gl_FragCoord.xy/aoResolution).r : 1.0;
}

// What the eye sees of reflected light (its colour fades at night, a lamp's bright pool keeps it; by day nothing
// changes). Emitted light is added after it and keeps its colour, like the sky's discs.
vec3 NightVisionOf(vec3 color)
{
    return nightVision(color, nightVisionFieldLuminance, nightVisionHalfLuminance, nightVisionSteepness,
        nightVisionStrength, nightVisionTint);
}

// The draw's place on the volume's screen: a render-space point through the scene's view-projection.
vec2 HazeScreenUv(vec3 worldPos)
{
    vec4 clip = hazeViewProjection*vec4(worldPos, 1.0);
    return (clip.w > 0.0) ? clip.xy/clip.w*0.5 + 0.5 : vec2(0.5);
}

// Aerial perspective: a surface's light reaches the camera dimmed by the haze in front of it (its transmittance), plus
// the light that haze scatters toward the camera (its in-scatter, seen through the eye's night vision as the sky's glow
// is). At the horizon that is the sky itself, so far things still dissolve into it; nearer, a hill ends as a pale,
// flat silhouette, never see-through. The camera sits at the origin of render space.
vec3 ApplyAerialPerspective(vec3 color, vec3 worldPos)
{
    float toPoint = length(worldPos);
    if (!(toPoint > 0.0))
    {
        return color;
    }
    vec3 inScatter;
    vec3 transmittance;
    hazeInFront(HazeScreenUv(worldPos), worldPos/toPoint, InteriorHazeDistance(worldPos, toPoint), hazeMultiplier,
        normalize(sunDirection), normalize(moonDirection), inScatter, transmittance);
    return color*transmittance + NightVisionOf(inScatter);
}

// The haze's transmittance to a point alone: an emitter seen through the haze (an additive sprite) is dimmed by it, its
// in-scatter already being in what lies behind.
vec3 AerialTransmittance(vec3 worldPos)
{
    float toPoint = length(worldPos);
    return (toPoint > 0.0) ? hazeTransmittance(HazeScreenUv(worldPos), worldPos/toPoint,
        InteriorHazeDistance(worldPos, toPoint), hazeMultiplier) : vec3(1.0);
}
