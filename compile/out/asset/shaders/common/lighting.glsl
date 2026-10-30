// The world's lighting, shared by the lit shaders (world_pbr and world_sprite), for #include "common/lighting": the
// frame's lighting uniforms (the renderer uploads the same values to every shader that includes this file), the
// Cook-Torrance BRDF, the sky's two directional lights (the sun and the moon) with the shadow levels' shadow, forward point
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

// The shadow levels of the sky's shadow light (the sun by day, the moon on bright nights; WorldShadowLevels.h): nested
// square boxes around the camera in the light's view, each shadowLevelFactor times as wide as the one before (32 m to
// 8 km by default), one layer each of a depth texture array read with the hardware's comparison. A point reads the
// smallest level that holds it; over the outer part of each level its shadow blends into the next level's, the last
// level's fades to none, and every shadow fades out over the reach's last stretch. A level the renderer kept from an
// earlier frame is read with the matrices it was rendered with.
#define SHADOW_MAX_LEVELS 8
uniform int shadowLevelCount;                         // the levels read this frame (0: none)
uniform mat4 shadowLevelMatrices[SHADOW_MAX_LEVELS];  // render space -> each level's light clip space, as its map holds
uniform vec2 shadowLevelOffsets[SHADOW_MAX_LEVELS];   // per level: the depth bias (depth units: half a texel) and the
                                                      // lookup's move off its surface at grazing incidence (metres)
uniform float shadowLevelEdge;        // a level is read up to this share of its half size (its margin at the edge)
uniform float shadowLevelBandStart;   // and blends into the next level from this share on
uniform float shadowLevelLogFactor;   // the natural log of the factor between the levels' widths
uniform vec2 shadowReachFade;         // the reach's fade, metres from the camera: where it starts and the reach itself
uniform sampler2DArrayShadow shadowLevels; // the levels' maps, one layer each (depth compared, bilinear)
uniform bool shadowLevelView;         // the debug view: every lit surface tinted by the level its shadow comes from
uniform float shadowStrength;  // 0..1, how much a shadowed light is darkened (the levels and the point shadow)
uniform vec3 shadowLightDirection; // unit direction TO the light the levels were rendered from
uniform float sunShadowFade;   // 0..1, how much the levels shadow the sun: 0 while the moon casts them or neither
uniform float moonShadowFade;  // 0..1, how much they shadow the moon. Both 0: the levels are not sampled at all

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

// The weather's wetness of surfaces (0 dry .. 1 soaked; ApplyWetness).
uniform float surfaceWetness;

// The eye's night vision (shaders/common/night_vision.glsl): dim reflected light loses its colour as it does for the
// eye; emitted light keeps it. The same values the sky pass gets.
uniform float nightVisionFieldLuminance; // the scene luminance the sky's light gives (linear, the game's light units)
uniform float nightVisionHalfLuminance;  // night vision's calibration on the game's light scale (source/NightVision.h)
uniform float nightVisionSteepness;
uniform float nightVisionStrength;       // 0 none (colourful nights) .. 1 the eye
uniform vec3  nightVisionTint;           // the rods' grey, luminance 1 (the world's blue shift)


// ---- Wet surfaces ----------------------------------------------------------------------------------------------
// How much a soaked surface of porosity 1 darkens (water fills its pores: the light it scatters is trapped and absorbed
// before it gets out), how much smoother any soaked surface turns (a film of water reflects like a polished surface),
// and the porosity of surfaces that give none of their own (models): half way.
const float WET_DARKENING = 0.7;
const float WET_SMOOTHING = 0.5;
const float WET_DEFAULT_POROSITY = 0.5;

// Wets a surface by the weather's wetness: most where it faces up, half where it is upright, none where it faces down
// (`normal`: its geometric normal, unit), darker by its porosity (0..1) and smoother. Leaves it as it is while dry.
void ApplyWetness(inout vec3 albedo, inout float roughness, vec3 normal, float porosity)
{
    float wet = surfaceWetness*clamp(0.5 + 0.5*normal.y, 0.0, 1.0);
    if (!(wet > 0.0))
    {
        return;
    }
    albedo *= 1.0 - WET_DARKENING*clamp(porosity, 0.0, 1.0)*wet;
    roughness = max(roughness*(1.0 - WET_SMOOTHING*wet), 0.045);
}


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


// ---- The levels' shadow ----------------------------------------------------------------------------------------
// The levels hold the casters' faces turned away from the light (their front faces are culled), so a lit surface never
// meets itself in a map. A lookup moves off its surface along its geometric normal, the more the more the surface is
// turned from the light (no surface shadows itself through a map's texels at a slant), and keeps only a small depth
// bias: a shadow then reaches the foot of what casts it, and a roof's shadow the top of the wall under it. Both are in
// texels of the level read, so every level is as tight as the first relative to its texel.

// Where a point's shadow comes from (WorldShadowLevels_GetLookup is its C mirror): the level read, below the count (or
// the count itself: none holds it), and the next level's share across the level's band (at the last level, the share
// that fades to none).
struct ShadowLevelChoice
{
    int level;
    float nextWeight;
};

// The larger of a point's |x| and |y| in a level's light clip coordinates: 1 at the box's edge.
float ShadowLevelBoxDistance(int level, vec3 worldPos)
{
    vec2 clip = (shadowLevelMatrices[level]*vec4(worldPos, 1.0)).xy; // orthographic: w is 1
    return max(abs(clip.x), abs(clip.y));
}

// The smallest level that holds a point, and the blend across its band. The levels are nearly concentric (a kept level's
// box may lie off the camera by its move limit and turn by its turn limit), so the first level's coordinates give the
// level by the factor between their widths to within one: the level's own matrix then decides, the next level taking a
// point its box leaves out (an estimate one level low); one level high happens only inside a band, where the next level
// holds the casters too.
ShadowLevelChoice ChooseShadowLevel(vec3 worldPos)
{
    ShadowLevelChoice choice;
    float firstBox = ShadowLevelBoxDistance(0, worldPos);
    int level = (firstBox <= shadowLevelEdge) ? 0
        : int(ceil(log(firstBox/shadowLevelEdge)/max(shadowLevelLogFactor, 1e-6)));
    level = clamp(level, 0, shadowLevelCount);
    float box = (level < shadowLevelCount) ? ShadowLevelBoxDistance(level, worldPos) : 2.0;
    if ((box > shadowLevelEdge) && (level < shadowLevelCount))
    {
        level += 1;
        box = (level < shadowLevelCount) ? ShadowLevelBoxDistance(level, worldPos) : 2.0;
    }
    choice.level = level;
    choice.nextWeight = clamp((box - shadowLevelBandStart)/max(shadowLevelEdge - shadowLevelBandStart, 1e-6), 0.0, 1.0);
    return choice;
}

// One level's shadow at a point, 0 (lit) .. 1 (shadowed): the point moved off its surface by the level's offset times
// slant (the sine of the surface's angle to the light), compared with the level's map by the hardware, which weighs the
// four texels around it bilinearly (a soft one-texel edge, so a coarse level's edge is no staircase). Beyond the far
// plane (nothing within the reach is) it reads as lit. The read sits in branches (neighbouring pixels may read other
// levels), where its implicit derivatives are undefined: harmless, the maps have no mipmaps and filter linearly at every
// scale (and cheaper than explicit gradients).
float SampleShadowLevel(int level, vec3 worldPos, vec3 normal, float slant)
{
    vec2 offsets = shadowLevelOffsets[level];
    vec3 proj = (shadowLevelMatrices[level]*vec4(worldPos + normal*(offsets.y*slant), 1.0)).xyz*0.5 + 0.5;
    if (proj.z > 1.0)
    {
        return 0.0;
    }
    float lit = texture(shadowLevels, vec4(proj.xy, float(level), proj.z - offsets.x));
    return 1.0 - lit;
}

// Returns 0 (lit) .. 1 (shadowed) for the fragment from the level that holds it, blended across its band into the next
// level's (or to none past the last), times the reach's fade. normal is the surface's geometric normal (unit), nDotL its
// cosine toward the shadow light (clamped at 0). Samples nothing while the levels shadow neither light (they did not run
// this frame).
float ComputeShadow(vec3 worldPos, vec3 normal, float nDotL)
{
    if ((shadowStrength <= 0.0) || (shadowLevelCount <= 0) || ((sunShadowFade <= 0.0) && (moonShadowFade <= 0.0)))
    {
        return 0.0;
    }
    float reachFade = 1.0 - clamp((length(worldPos) - shadowReachFade.x)/max(shadowReachFade.y - shadowReachFade.x, 1e-6),
        0.0, 1.0);
    ShadowLevelChoice choice = ChooseShadowLevel(worldPos);
    if ((reachFade <= 0.0) || (choice.level >= shadowLevelCount))
    {
        return 0.0;
    }
    float slant = sqrt(max(1.0 - nDotL*nDotL, 0.0));
    float shadow = SampleShadowLevel(choice.level, worldPos, normal, slant);
    if (choice.nextWeight > 0.0)
    {
        float next = ((choice.level + 1) < shadowLevelCount)
            ? SampleShadowLevel(choice.level + 1, worldPos, normal, slant) : 0.0;
        shadow = mix(shadow, next, choice.nextWeight);
    }
    return shadow*reachFade;
}

// The levels' debug view (shadowLevelView): a colour by the level a point's shadow comes from, mixed across the bands,
// times the picture's own brightness; black where no level holds the point (beyond the levels or the reach).
const vec3 SHADOW_LEVEL_VIEW_COLORS[SHADOW_MAX_LEVELS] = vec3[]
(
    vec3(1.0, 0.25, 0.25), vec3(0.25, 1.0, 0.25), vec3(0.3, 0.45, 1.0), vec3(1.0, 1.0, 0.2),
    vec3(1.0, 0.3, 1.0), vec3(0.2, 1.0, 1.0), vec3(1.0, 0.6, 0.2), vec3(1.0, 1.0, 1.0)
);

vec3 ShadowLevelViewColor(vec3 color, vec3 worldPos)
{
    if (!shadowLevelView || (shadowLevelCount <= 0))
    {
        return color;
    }
    float reachFade = 1.0 - clamp((length(worldPos) - shadowReachFade.x)/max(shadowReachFade.y - shadowReachFade.x, 1e-6),
        0.0, 1.0);
    ShadowLevelChoice choice = ChooseShadowLevel(worldPos);
    if ((reachFade <= 0.0) || (choice.level >= shadowLevelCount))
    {
        return color*0.1;
    }
    vec3 next = ((choice.level + 1) < shadowLevelCount) ? SHADOW_LEVEL_VIEW_COLORS[choice.level + 1] : vec3(0.1);
    vec3 hue = mix(SHADOW_LEVEL_VIEW_COLORS[choice.level], next, choice.nextWeight);
    return color*mix(vec3(0.1), hue, reachFade);
}

// How much the levels darken the light that cast them at a point (0..1): their shadow times the shadow strength.
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

// The ambient pre-pass's contact shadow at this pixel (shaders/ao: 1 in one, 0 lit), of the light the levels were
// rendered from; 0 while no pre-pass ran or it did not see this draw's surface (drawInPrepass).
float ScreenContactShadow()
{
    return (aoMapActive && drawInPrepass) ? 1.0 - texture(ambientOcclusionMap, gl_FragCoord.xy/aoResolution).b : 0.0;
}

// How much the levels darken the light that cast them on a model's or the terrain's surface (0..1), read off the
// triangle's own face (FaceNormal): the maps hold the faces turned away from the light, so such a face lies in its own
// shadow and takes all of it, and any other is read off its face. An interpolated or normal-mapped normal can lean
// toward the light where its face leans away, and read the face's own entry in the map as lit: specks and bands of light
// inside shade. (So a rounded low-poly shape's terminator follows its faces.) The screen's contact shadow darkens it too,
// where the levels' texels leave a seam at the foot of what casts a shadow. Nothing while the levels shadow neither
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

// The sky's two lights through the plain BRDF: the sun, then (at night) the moon. The levels' shadow darkens the
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
