// The world's lighting, shared by the lit shaders (world_pbr, world_crt, world_sprite, world_terrain), for #include
// "common/lighting": the frame's lighting uniforms (the renderer uploads the same values to every shader that includes
// this file), the Cook-Torrance BRDF, the sky's two directional lights (the sun and the moon) with the shadow levels'
// shadow and the terrain's shadow at any distance (common/far_shadow) and a lightning bolt's light, forward lights
// (points and spots), the shadow-casting point light with its cube shadow and the shadowed spots with their maps, a
// screen's light, the sky's ambient light with the
// screen-space AO and interior lighting (common/interior: what roofs keep out, rooms' own light), what a glossy surface
// mirrors (and what it hands the reflection pass: common/reflections), the weather's wetness and puddles (common/rain:
// what keeps the rain out; common/puddles: the puddles, the sky mirrored in water), the eye's night vision and the haze
// in front of every surface (the aerial perspective and the ground haze, common/haze).
//
// THE SKY'S LIGHT (one light scale): the sun and the moon reach a surface through the air (their light above the air
// times the transmittance toward them at the surface's own height: reddened low over the horizon, gone once the disc has
// set there), and its ambient is the sky's own light, the sky's irradiance from above and the ground's bounce from below
// (AmbientLight), plus the moonless night's own light. The atmosphere pass writes that light by height into the haze
// volume's last row every frame (SkyLightAt reads it at the fragment's height); without it (the atmosphere's tables off
// or unavailable) the uniforms hold the light at the camera, the C mirror's.
//
// Two families of light functions: the plain ones (SkyLight, LocalLights) the PBR shader uses, and the wrapped ones
// (SkyLightWrapped, LocalLightsWrapped) sprites use, whose diffuse part reaches past the terminator ("wrap lighting",
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
#include "common/puddles"
#include "common/reflections"
#include "common/far_shadow"

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
uniform float sunShadowFade;   // 0..1, how much the levels and the far shadows shadow the sun: 0 while the moon casts them
                               // or neither
uniform float moonShadowFade;  // 0..1, how much they shadow the moon. Both 0: no map is sampled at all

// Screen-space ambient occlusion, computed in a pre-pass (shaders/ao) and applied to ONLY the ambient term (so it never
// dims the direct sun / point lights). aoMapActive false => no SSAO this frame.
uniform sampler2D ambientOcclusionMap; // the ambient pre-pass (shaders/ao): R the AO multiplier (1 = open, < 1 = occluded), G
                                       // the sky's visibility, B the contact light
uniform bool aoMapActive;
uniform vec2 aoResolution;             // scene-target size in pixels, to map gl_FragCoord -> AO map uv
uniform bool skyVisibilityOnScreen;    // the pre-pass's G holds the sky's visibility this frame (else each surface reads it)
uniform bool drawInPrepass;            // per draw: the pre-pass saw this draw's surface at its pixels (false: it faded out of
                                       // the G-buffer, and the pixels hold what lies behind it)

// Forward lights (points and spots), culled + uploaded per object by the renderer (the strongest that reach this
// object). MAX_FORWARD_LIGHTS should equal WORLD_MAX_FORWARD_LIGHTS in WorldLightCulling.h. A mismatch is safe: the
// loops below clamp to the array size, and the renderer checks the arrays at load and never uploads more than fit.
#define MAX_FORWARD_LIGHTS 8
uniform int forwardLightCount;                            // active entries in the arrays below (0..MAX)
uniform vec3 forwardLightPositions[MAX_FORWARD_LIGHTS];   // render space
uniform vec3 forwardLightRadiances[MAX_FORWARD_LIGHTS];   // linear colour * intensity * its flicker this frame
uniform float forwardLightRanges[MAX_FORWARD_LIGHTS];     // reach radius (attenuation falls to 0 here)
uniform vec3 forwardLightDirections[MAX_FORWARD_LIGHTS];  // a spot's axis, the way it shines (unit); a point's (0, 0, 0)
uniform vec2 forwardLightCones[MAX_FORWARD_LIGHTS];       // a spot's cone falloff (LightCone); a point's (0, 1)
uniform int forwardLightShadows[MAX_FORWARD_LIGHTS];      // a shadowed spot's map (its layer of spotShadowMaps); -1 none

// ONE shadow-casting point light, shaded SEPARATELY from the culled set above (which excludes it) so it can sample its
// own omnidirectional (cube) shadow map. pointShadowActive false => no shadow-casting point light.
uniform bool pointShadowActive;
uniform vec3 pointShadowLightPos;       // render space
uniform vec3 pointShadowLightRadiance;  // linear colour * intensity * its flicker this frame
uniform float pointShadowLightRange;    // reach radius
uniform samplerCube pointShadowCube;    // packed (RGBA8) linear distance from the light, per direction
uniform float pointShadowFar;           // far range the stored distance was normalized by
uniform float pointShadowBias;          // world-space depth bias to combat shadow acne

// The shadowed spots' maps (at most MAX_SPOT_SHADOWS: the renderer's WORLD_RENDERER_MAX_SPOT_SHADOWS): a perspective depth
// map from each light along its axis, a layer of spotShadowMaps each (source/WorldLocalShadows.h). The shadowed spots are
// forward lights like the others, culled and ranked per draw; forwardLightShadows names a light's layer, and the loop
// darkens it by its map. (Shaded in a term of their own, a second copy of the BRDF per spot, they cost the scene pass
// about a third more where they light.)
#define MAX_SPOT_SHADOWS 2
uniform mat4 spotShadowMatrices[MAX_SPOT_SHADOWS];    // render space -> its map's clip space
uniform vec2 spotShadowLookups[MAX_SPOT_SHADOWS];     // per metre of depth along the axis: the lookup's move off its
                                                      // surface at grazing incidence; and the depth bias times the depth
uniform float spotShadowTexel;                        // a texel of the maps in their texture coordinates (1 / their size)
uniform sampler2DArrayShadow spotShadowMaps;          // the maps (depth compared, bilinear)

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

// Reflections (common/reflections, common/reflection_output): while reflectionsDeferred is on, the reflection pass adds
// what this pass's mirroring surfaces mirror (the screen around them, the sky where it has nothing), and they hand it
// their data instead of adding the sky's reflection themselves; off, they add the sky's. hasReflections, per draw: the
// object's glossy surfaces mirror their surroundings (its placement's has_reflections); water on wet surfaces mirrors
// whatever the draw.
uniform bool reflectionsDeferred;
uniform bool hasReflections;

// The drawn clouds' shadow: a map around the camera of the share of the light that lights them (the sun, or the moon at
// night) that passes them, for the points of a level plane at their base (shaders/clouds/shadow.frag: R through every
// layer, G the low layer's share, B the upper layers'). A surface reads it where its ray toward that light crosses the
// plane; beyond the map, the clouds' share on average.
uniform bool cloudShadowActive;   // false: no map this frame (the light's share is in the sky's light already)
uniform sampler2D cloudShadowMap;
uniform vec4 cloudShadowFrame;    // the map's centre on the plane (render space x, z), 1 over its side, the plane's height
                                  // in render space
uniform float cloudShadowTop;     // the low layer's top's height in render space
uniform vec3 cloudShadowLight;    // the light it was made for (unit)
uniform float cloudShadowFallback; // the share of that light beyond the map
uniform bool cloudShadowIsMoon;   // the map shades the moon's light (else the sun's)

// A screen's light, one per draw (the nearest shown screen that glows and reaches the draw; WorldRendererSurfaces.c): its
// glass lights what is in front of it like a small window, in its picture's mean colour. No shadow: it lights through
// what stands in front of it.
uniform bool screenLightActive;
uniform vec3 screenLightPosition;     // its glass's centre, render space
uniform vec3 screenLightNormal;       // the way its glass faces (unit)
uniform vec4 screenLightShape;        // its glass's area (square metres), its reach (metres), the mipmap level its
                                      // picture's mean is read at, its whiteness (a CRT's squeezed beam)
uniform float screenLightGlow;        // its glow per unit of its picture's colour (the picture's emission, its light)
uniform sampler2D screenLightPicture; // its picture, sRGB as a screen's emissive map

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
// and the porosity of surfaces that give none of their own (sprites): half way. A wet film mirrors the sky on
// WET_FILM_REFLECTION of itself (its roughness blurs the rest into the light it already has); a puddle mirrors it whole
// over a bottom PUDDLE_DARKENING darker still, with still water's smoothness.
const float WET_DARKENING = 0.7;
const float WET_SMOOTHING = 0.5;
const float WET_DEFAULT_POROSITY = 0.5;
const float WET_FILM_REFLECTION = 0.35;
const float PUDDLE_DARKENING = 0.3;
const float PUDDLE_ROUGHNESS = 0.045;

// Wets a surface by the weather's wetness where the rain reaches it (common/rain's rain map: covered surfaces stay dry):
// most where it faces up, half where it is upright, none where it faces down (surfaceNormal: its geometric normal, unit),
// darker by its porosity (0..1) and smoother; on surfaces facing up, puddles by its puddles (0..1), still water over a
// darker bottom whose normal n becomes the water's (rippled while it rains). `water` receives the share of the surface
// that mirrors the sky (WaterReflection). Leaves it as it is while dry. Call it in uniform control flow (the puddle
// mask's reads take their derivatives first).
void ApplyWetness(inout vec3 albedo, inout float roughness, inout vec3 n, out float water, vec3 worldPos,
    vec3 surfaceNormal, float porosity, float puddles)
{
    water = 0.0;
    if (!(surfaceWetness > 0.0))
    {
        return; // a uniform: the same for every fragment, so what follows stays in uniform control flow
    }
    RainPuddlePlace place = RainPuddlePlaceAt(worldPos);
    float wet = surfaceWetness*clamp(0.5 + 0.5*surfaceNormal.y, 0.0, 1.0);
    if (!(wet > 0.0))
    {
        return;
    }
    float open = RainOpenness(worldPos, surfaceNormal)*RainTopOpenness(worldPos.y);
    wet *= open;
    if (!(wet > 0.0))
    {
        return;
    }
    albedo *= 1.0 - WET_DARKENING*clamp(porosity, 0.0, 1.0)*wet;
    roughness = max(roughness*(1.0 - WET_SMOOTHING*wet), 0.045);
    water = WET_FILM_REFLECTION*wet;
    float puddle = RainPuddleShare(place, surfaceNormal, puddles)*open;
    if (puddle > 0.0)
    {
        albedo *= 1.0 - PUDDLE_DARKENING*puddle;
        roughness = mix(roughness, PUDDLE_ROUGHNESS, puddle);
        n = normalize(mix(n, RainWaterNormal(worldPos), puddle));
        water = mix(water, 1.0, puddle);
    }
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

// Schlick's Fresnel for light mirrored from all around (not one light's half vector): a rough surface's grazing
// reflectance rises only to 1 - its roughness, its blurred reflection spreading over its other directions.
vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float rough)
{
    return f0 + (max(vec3(1.0 - rough), f0) - f0)*pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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

// How much the shadow light is darkened at a point (0..1): the darker of the levels' shadow and the terrain's far
// shadow (common/far_shadow: the near casters are the levels', the far terrain the far shadows'), times the shadow
// strength. shadowPos is where the shadow maps are read: the fragment's own position, or for a billboard the matching
// point of the sprite as the light sees it (its shadow-map entry faces the light); shadowNormal the geometric normal there
// (unit), and nDotL its cosine toward the shadow light (clamped at 0), which set how far the levels' lookup moves off the
// surface.
float SkyShadowAmount(vec3 shadowPos, vec3 shadowNormal, float nDotL)
{
    float shadow = max(ComputeShadow(shadowPos, shadowNormal, nDotL), FarShadowAt(shadowPos));
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

// How much the shadow light is darkened on a model's or the terrain's surface (0..1), read off the triangle's own face
// (FaceNormal): the levels' maps hold the faces turned away from the light, so such a face lies in its own shadow and
// takes all of it, and any other is read off its face. An interpolated or normal-mapped normal can lean toward the light
// where its face leans away, and read the face's own entry in the map as lit: specks and bands of light inside shade.
// (So a rounded low-poly shape's terminator follows its faces.) The screen's contact shadow darkens it too, where the
// levels' texels leave a seam at the foot of what casts a shadow, and the terrain's far shadow (SkyShadowAmount).
// Nothing while neither light is shadowed.
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

// The least a light's ray climbs per metre for the clouds' shadow map (shaders/clouds/shadow.frag's, the C side's
// WORLD_CLOUDS_SHADOW_MIN_UP).
const float CLOUD_SHADOW_MIN_UP = 0.02;

// The share of the clouds' light that passes them toward a render-space point: the map where the point's ray toward the
// light crosses the clouds' base, fading into the average share over the map's outer tenth. A point inside or above the
// low layer takes the low layer's share only by the part of its room above the point (as if its cloud were spread
// evenly through it), and the upper layers' share.
float CloudShadowAt(vec3 worldPos)
{
    float up = max(cloudShadowLight.y, CLOUD_SHADOW_MIN_UP);
    vec2 crossing = worldPos.xz + cloudShadowLight.xz*((cloudShadowFrame.w - worldPos.y)/up);
    vec2 uv = (crossing - cloudShadowFrame.xy)*cloudShadowFrame.z + 0.5;
    float edge = 2.0*max(abs(uv.x - 0.5), abs(uv.y - 0.5)); // 0 at the centre, 1 at the edge
    vec3 mapped = texture(cloudShadowMap, uv).rgb;
    float passed = mapped.r;
    float fallback = cloudShadowFallback;
    if (worldPos.y > cloudShadowFrame.w)
    {
        float above = clamp((cloudShadowTop - worldPos.y)/max(cloudShadowTop - cloudShadowFrame.w, 1.0), 0.0, 1.0);
        passed = pow(max(mapped.g, 1e-6), above)*mapped.b;
        fallback = pow(max(cloudShadowFallback, 1e-6), above);
    }
    return mix(passed, fallback, smoothstep(0.9, 1.0, edge));
}

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
// in render space), or the camera's while the volume holds none; the drawn clouds' shadow on the light that lights them.
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
        if (cloudShadowActive)
        {
            float passed = CloudShadowAt(worldPos);
            if (cloudShadowIsMoon)
            {
                light.moon *= passed;
            }
            else
            {
                light.sun *= passed;
            }
        }
        return light;
    }
    light.sun = sunLight;
    light.moon = moonLight;
    light.sky = skyIrradiance;
    light.ground = groundIrradiance;
    return light;
}

// The sky's two lights through the plain BRDF: the sun, then (at night) the moon. The levels' shadow darkens the
// direct term of the light that cast it (by its fade), never the ambient. A lightning bolt's channel lights it too, while
// one stands, unshadowed (common/rain's boltLight).
vec3 SkyLight(SkyLightValues light, vec3 n, vec3 v, float shadowAmount, vec3 albedo, vec3 f0, float rough, float metal)
{
    vec3 direct = DirectionalLight(n, v, normalize(sunDirection), light.sun, 1.0 - shadowAmount*sunShadowFade, albedo,
        f0, rough, metal);
    if (isMoonLit)
    {
        direct += DirectionalLight(n, v, normalize(moonDirection), light.moon, 1.0 - shadowAmount*moonShadowFade, albedo,
            f0, rough, metal);
    }
    if (any(greaterThan(boltLight, vec3(0.0))))
    {
        direct += DirectionalLight(n, v, boltDirection, boltLight, 1.0, albedo, f0, rough, metal);
    }
    return direct;
}

// The sky's two lights with wrap lighting (WrappedLight), shadowed like SkyLight, and a bolt's like SkyLight's.
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
    if (any(greaterThan(boltLight, vec3(0.0))))
    {
        direct += WrappedLight(n, v, boltDirection, boltLight, wrap, albedo, f0, rough, metal);
    }
    return direct;
}

// What a glossy surface at a point mirrors toward the direction r (unit): the sky above (the sky view's, as water mirrors
// it: common/puddles' RainSkyRadiance, its even sky the fallback) and the ground's bounce below as an even light, joined
// over a band at the horizon, tinted as the ambient is, with the moonless night's own light; under a roof all of it
// times the share of the sky the surface sees at its visibility (skyVisibility, as AmbientLight takes it) and its zones'
// sky scale, plus the zones' own light (a room's even light all around).
vec3 EnvironmentRadiance(SkyLightValues light, vec3 r, vec3 worldPos, vec3 surfaceNormal, float skyVisibility)
{
    vec3 up = normalize(vec3(r.x, max(r.y, 0.02), r.z));
    vec3 sky = RainSkyRadiance(up, normalize(sunDirection), normalize(moonDirection), light.sky/PI);
    vec3 around = mix(light.ground/PI, sky, smoothstep(-0.05, 0.05, r.y))*ambientTint + nightLight;
    if (!interiorActive)
    {
        return around;
    }
    InteriorZoneLight zones = InteriorZonesAt(worldPos, surfaceNormal);
    return around*InteriorSkyShare(skyVisibility)*zones.skyScale + zones.ambient;
}

// The sky mirrored by the share `water` of a wet surface (ApplyWetness), as the sky's light on surfaces is (its tint
// in): common/puddles' RainWaterReflection, with the even sky of the sky's light at the surface as the fallback.
vec3 WaterReflection(SkyLightValues light, vec3 n, vec3 v, float water)
{
    return RainWaterReflection(n, v, water, normalize(sunDirection), normalize(moonDirection), light.sky/PI)
        *ambientTint;
}

// What a surface mirrors at a pixel, as it hands it to the reflection pass (common/reflection_output): the weight (the
// share of the mirrored light that reaches the eye, before the haze in front of the surface), what it mirrors where the
// screen has no answer (the sky and the ground's bounce, linear), the direction it mirrors along (unit, render space) and
// the share of its reflection the screen may answer (by its roughness: ReflectionScreenShare).
struct SurfaceReflection
{
    vec3 weight;
    vec3 sky;
    vec3 direction;
    float screenShare;
};

// A surface that mirrors nothing.
SurfaceReflection NoReflection()
{
    SurfaceReflection reflection;
    reflection.weight = vec3(0.0);
    reflection.sky = vec3(0.0);
    reflection.direction = vec3(0.0, 1.0, 0.0);
    reflection.screenShare = 0.0;
    return reflection;
}

// A glossy object's mirror (its has_reflections): the light from all around it mirrors by its Fresnel reflectance for
// light from every direction (FresnelSchlickRoughness: a rough surface's grazing mirror stays dim), times its occlusion;
// the sky's and the ground's light all around (EnvironmentRadiance) where the screen has no answer. fresnel receives the
// reflectance, which the caller takes from its ambient's diffuse light (what mirrors is not scattered too).
SurfaceReflection GlossyReflection(SkyLightValues light, vec3 n, vec3 v, vec3 f0, float rough, float occlusion,
    vec3 worldPos, vec3 surfaceNormal, float skyVisibility, out vec3 fresnel)
{
    SurfaceReflection reflection;
    fresnel = FresnelSchlickRoughness(clamp(dot(n, v), 0.0, 1.0), f0, rough);
    reflection.direction = reflect(-v, n);
    reflection.weight = fresnel*occlusion;
    reflection.sky = EnvironmentRadiance(light, reflection.direction, worldPos, surfaceNormal, skyVisibility);
    reflection.screenShare = ReflectionScreenShare(rough);
    return reflection;
}

// The water on a wet surface (ApplyWetness's share `water`, roughness its roughness after it), as WaterReflection mirrors
// the sky: water's Fresnel reflectance along the direction kept above the horizon; nothing while the rain is off or the
// surface is dry.
SurfaceReflection WetReflection(SkyLightValues light, vec3 n, vec3 v, float water, float roughness)
{
    if (!rainActive || !(water > 0.0))
    {
        return NoReflection();
    }
    SurfaceReflection reflection;
    reflection.direction = RainWaterDirection(n, v);
    reflection.weight = vec3(RainWaterFresnel(n, v)*water);
    reflection.sky = RainSkyRadiance(reflection.direction, normalize(sunDirection), normalize(moonDirection),
        light.sky/PI)*ambientTint;
    reflection.screenShare = ReflectionScreenShare(roughness);
    return reflection;
}

// Two reflections of one surface as one (a glossy object's mirror under a wet film): their weights added, what they
// mirror where the screen has no answer by their weights, the direction of the stronger, their screen shares by their
// weights.
SurfaceReflection CombineReflections(SurfaceReflection a, SurfaceReflection b)
{
    float weightA = a.weight.r + a.weight.g + a.weight.b;
    float weightB = b.weight.r + b.weight.g + b.weight.b;
    if (!((weightA + weightB) > 0.0))
    {
        return NoReflection();
    }
    SurfaceReflection reflection;
    reflection.weight = a.weight + b.weight;
    reflection.sky = (a.weight*a.sky + b.weight*b.sky)/max(reflection.weight, vec3(1e-6));
    reflection.direction = (weightB > weightA) ? b.direction : a.direction;
    reflection.screenShare = mix(a.screenShare, b.screenShare, weightB/(weightA + weightB));
    return reflection;
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


// ---- Local lights: points and spots ----------------------------------------------------------------------------
// Where a surface reads the local lights' shadows: its own point (a model, the terrain, the water: SurfaceShadowLookups),
// or for a billboard the matching point of its quad as each light sees it (each light's map holds the quad facing that
// light), with the geometric normal there.
struct LocalShadowLookups
{
    vec3 cubePosition;
    vec3 cubeNormal;
    vec3 spotPositions[MAX_SPOT_SHADOWS];
    vec3 spotNormals[MAX_SPOT_SHADOWS];
};

// Every local light's shadow read at the surface's own point: the cube's with the normal the surface is shaded with,
// each spot's off the triangle's own face (FaceNormal: what the spots' maps hold, as the shadow levels read theirs; a
// normal map's bumps would jitter the lookups).
LocalShadowLookups SurfaceShadowLookups(vec3 position, vec3 cubeNormal, vec3 faceNormal)
{
    LocalShadowLookups lookups;
    lookups.cubePosition = position;
    lookups.cubeNormal = cubeNormal;
    lookups.spotPositions[0] = position;
    lookups.spotNormals[0] = faceNormal;
    lookups.spotPositions[1] = position;
    lookups.spotNormals[1] = faceNormal;
    return lookups;
}

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

// A local light's falloff at a distance: a physical 1/d^2, smoothly windowed to exactly 0 at the light's range so the
// shaded set agrees with what the CPU reach-culler decided affects an object (no hard popping at the edge).
float LocalLightFalloff(float dist, float lightRange)
{
    float range = max(lightRange, 1e-3);
    float window = clamp(1.0 - pow(dist/range, 4.0), 0.0, 1.0);
    return (window*window)/(dist*dist + 1.0);
}

// A light's cone at a point it lights: with c the cosine between the way from the light to the point and the light's
// axis, t = clamp(c*cone.x + cone.y, 0, 1) is 1 within a spot's inner angle and 0 past its outer (the renderer's
// WorldRendererLighting_GetConeFalloff), and the light is t's smoothstep: no edge where the fade begins or ends. A point's
// cone (0, 1) is exactly 1 every way. toLight is the unit direction from the point to the light.
float LightCone(vec3 toLight, vec3 axis, vec2 cone)
{
    float t = clamp(dot(-toLight, axis)*cone.x + cone.y, 0.0, 1.0);
    return t*t*(3.0 - 2.0*t);
}

// One local light through the plain BRDF with LocalLightFalloff and its cone (LightCone).
vec3 LocalLight(vec3 n, vec3 v, vec3 worldPos, vec3 lightPos, vec3 radiance, float lightRange, vec3 axis, vec2 cone,
    vec3 albedo, vec3 f0, float rough, float metal)
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

    return (kDp*albedo/PI + specp)*radiance*ndlp*LocalLightFalloff(dist, lightRange)*LightCone(lp, axis, cone);
}

// 0 (lit) .. 1 (shadowed) for a shadowed spot (its map's layer `spot`, its render-space position lightPos and its axis) at
// a point with the geometric normal `normal`, turned toward the light (a two-sided quad's back faces it too)
// (source/WorldLocalShadows.h): the point moved off its surface by the map's texels there times the slant (the sine of the
// surface's angle to the light), projected onto the spot's map, and
// compared, less the depth bias, with 3 x 3 taps a texel apart, each weighing four texels bilinearly by the hardware (a
// soft edge some four texels wide). A point beyond the map (outside its field of view, past its far plane, behind the
// light) reads as lit. The reads sit in branches (only where the spot lights the surface), where implicit derivatives are
// undefined: harmless, the maps have no mipmaps and filter linearly at every scale.
float SpotShadowAmount(int spot, vec3 lightPos, vec3 axis, vec3 position, vec3 normal)
{
    vec3 toPoint = position - lightPos;
    float depth = max(dot(toPoint, axis), 1e-3);
    vec3 toLight = -toPoint/max(length(toPoint), 1e-4);
    vec3 facing = (dot(normal, toLight) < 0.0) ? -normal : normal;
    float nDotL = clamp(dot(facing, toLight), 0.0, 1.0);
    float slant = sqrt(max(1.0 - nDotL*nDotL, 0.0));
    vec2 lookup = spotShadowLookups[spot];
    vec4 clip = spotShadowMatrices[spot]*vec4(position + facing*(lookup.x*depth*slant), 1.0);
    if (!(clip.w > 0.0))
    {
        return 0.0;
    }
    vec3 proj = clip.xyz/clip.w*0.5 + 0.5;
    if (any(lessThan(proj.xy, vec2(0.0))) || any(greaterThan(proj.xy, vec2(1.0))) || (proj.z > 1.0))
    {
        return 0.0;
    }
    float compare = proj.z - lookup.y/depth;
    float lit = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            lit += texture(spotShadowMaps, vec4(proj.xy + vec2(float(x), float(y))*spotShadowTexel, float(spot), compare));
        }
    }
    return 1.0 - lit/9.0;
}

// One local light with wrap lighting (WrappedLight), LocalLightFalloff and its cone.
vec3 LocalLightWrapped(vec3 n, vec3 v, vec3 worldPos, vec3 lightPos, vec3 radiance, float lightRange, vec3 axis,
    vec2 cone, float wrap, vec3 albedo, vec3 f0, float rough, float metal)
{
    vec3 toLight = lightPos - worldPos;
    float dist = length(toLight);
    vec3 lp = toLight/max(dist, 1e-4);
    return WrappedLight(n, v, lp, radiance, wrap, albedo, f0, rough, metal)*LocalLightFalloff(dist, lightRange)
        *LightCone(lp, axis, cone);
}

// How much a shadowed light's term is left by its shadow (1 lit .. 1 - shadowStrength fully shadowed).
float LocalShadowVisibility(float shadow)
{
    return 1.0 - shadow*clamp(shadowStrength, 0.0, 1.0);
}

// Where a shadowed spot's map is read for a surface: the lookups of the spot of that layer, chosen by constant indices (an
// index the compiler cannot see through would put the lookups' arrays in memory for every fragment: some 60 % more time
// for the scene pass).
#if MAX_SPOT_SHADOWS != 2
#error "SpotLookupPosition and SpotLookupNormal choose between two shadowed spots: give each of MAX_SPOT_SHADOWS its case"
#endif
vec3 SpotLookupPosition(LocalShadowLookups lookups, int layer)
{
    return (layer == 0) ? lookups.spotPositions[0] : lookups.spotPositions[1];
}

vec3 SpotLookupNormal(LocalShadowLookups lookups, int layer)
{
    return (layer == 0) ? lookups.spotNormals[0] : lookups.spotNormals[1];
}

// A forward light's term darkened by its spot's map, where it has one and lights the surface.
vec3 ShadowForwardLight(int i, vec3 light, LocalShadowLookups lookups)
{
    int layer = forwardLightShadows[i];
    if ((layer < 0) || !any(greaterThan(light, vec3(0.0))))
    {
        return light;
    }
    return light*LocalShadowVisibility(SpotShadowAmount(layer, forwardLightPositions[i], forwardLightDirections[i],
        SpotLookupPosition(lookups, layer), SpotLookupNormal(lookups, layer)));
}

// The local lights through the plain BRDF: the reach-culled set the renderer chose for this object (the count is
// clamped to the arrays' size so a bad upload can never index past them), a shadowed spot among them darkened by its map,
// then the shadow-casting point light, darkened by its own cube shadow (shaded here rather than in the culled loop so it
// can sample the cube; the renderer excludes it from that loop). The shadows are read where lookups says
// (SurfaceShadowLookups: the fragment's own point, for models).
vec3 LocalLights(vec3 n, vec3 v, vec3 worldPos, LocalShadowLookups lookups, vec3 albedo, vec3 f0, float rough,
    float metal)
{
    vec3 localTotal = vec3(0.0);
    int forwardLimit = min(forwardLightCount, MAX_FORWARD_LIGHTS);
    for (int i = 0; i < forwardLimit; i++)
    {
        localTotal += ShadowForwardLight(i, LocalLight(n, v, worldPos, forwardLightPositions[i],
            forwardLightRadiances[i], forwardLightRanges[i], forwardLightDirections[i], forwardLightCones[i], albedo, f0,
            rough, metal), lookups);
    }
    if (pointShadowActive)
    {
        float visibility = LocalShadowVisibility(ComputePointShadow(lookups.cubePosition, lookups.cubeNormal));
        localTotal += LocalLight(n, v, worldPos, pointShadowLightPos, pointShadowLightRadiance, pointShadowLightRange,
            vec3(0.0), vec2(0.0, 1.0), albedo, f0, rough, metal)*visibility;
    }
    return localTotal;
}

// The local lights with wrap lighting (LocalLightWrapped), shadowed like LocalLights.
vec3 LocalLightsWrapped(vec3 n, vec3 v, vec3 worldPos, LocalShadowLookups lookups, float wrap, vec3 albedo, vec3 f0,
    float rough, float metal)
{
    vec3 localTotal = vec3(0.0);
    int forwardLimit = min(forwardLightCount, MAX_FORWARD_LIGHTS);
    for (int i = 0; i < forwardLimit; i++)
    {
        localTotal += ShadowForwardLight(i, LocalLightWrapped(n, v, worldPos, forwardLightPositions[i],
            forwardLightRadiances[i], forwardLightRanges[i], forwardLightDirections[i], forwardLightCones[i], wrap,
            albedo, f0, rough, metal), lookups);
    }
    if (pointShadowActive)
    {
        float visibility = LocalShadowVisibility(ComputePointShadow(lookups.cubePosition, lookups.cubeNormal));
        localTotal += LocalLightWrapped(n, v, worldPos, pointShadowLightPos, pointShadowLightRadiance,
            pointShadowLightRange, vec3(0.0), vec2(0.0, 1.0), wrap, albedo, f0, rough, metal)*visibility;
    }
    return localTotal;
}


// ---- A screen's light -----------------------------------------------------------------------------------------
// The mean colour of the screen's picture (linear): four by four taps at the mipmap level where it spans about four
// texels a side (as the picture's mipmaps average it, then decoded from sRGB), whitened as its beam is.
vec3 ScreenLightColor()
{
    vec3 sum = vec3(0.0);
    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            sum += textureLod(screenLightPicture, (vec2(float(x), float(y)) + 0.5)/4.0, screenLightShape.z).rgb;
        }
    }
    vec3 mean = pow(max(sum/16.0, vec3(0.0)), vec3(2.2));
    return mix(mean, vec3(1.0), clamp(screenLightShape.w, 0.0, 1.0));
}

// The screen's light at a render-space point on a surface facing it: its glass's radiance (its glow times its picture's
// colour) times its area and its cosine toward the point, over the distance squared plus the area over pi (a disc of that
// area's exact light on its axis, so it never blows up at the glass), faded to nothing at its reach; 0 behind the glass.
// l receives the direction toward the glass's centre, widen how much the glass's size seen from there widens a
// highlight (its half size over the distance, as a roughness).
vec3 ScreenLightIrradiance(vec3 worldPos, out vec3 l, out float widen)
{
    vec3 toScreen = screenLightPosition - worldPos;
    float dist2 = dot(toScreen, toScreen);
    float dist = sqrt(dist2);
    l = (dist > 1e-4) ? toScreen/dist : screenLightNormal;
    float area = max(screenLightShape.x, 0.0);
    widen = 0.5*sqrt(area)/max(dist, 1e-3);
    float facing = dot(screenLightNormal, -l);
    if (!(facing > 0.0) || !(dist < screenLightShape.y))
    {
        return vec3(0.0);
    }
    float window = clamp(1.0 - pow(dist/screenLightShape.y, 4.0), 0.0, 1.0);
    return ScreenLightColor()*screenLightGlow*(area*facing/(dist2 + area/PI))*window*window;
}

// The draw's screen light through the plain BRDF, its specular widened by the glass's size (a glossy desk shows a soft
// patch, not a pinpoint); nothing while no screen lights the draw.
vec3 ScreenLight(vec3 n, vec3 v, vec3 worldPos, vec3 albedo, vec3 f0, float rough, float metal)
{
    if (!screenLightActive)
    {
        return vec3(0.0);
    }
    vec3 l;
    float widen;
    vec3 irradiance = ScreenLightIrradiance(worldPos, l, widen);
    return DirectionalLight(n, v, l, irradiance, 1.0, albedo, f0, clamp(rough + widen, rough, 1.0), metal);
}

// The draw's screen light with wrap lighting (WrappedLight), as ScreenLight.
vec3 ScreenLightWrapped(vec3 n, vec3 v, vec3 worldPos, float wrap, vec3 albedo, vec3 f0, float rough, float metal)
{
    if (!screenLightActive)
    {
        return vec3(0.0);
    }
    vec3 l;
    float widen;
    vec3 irradiance = ScreenLightIrradiance(worldPos, l, widen);
    return WrappedLight(n, v, l, irradiance, wrap, albedo, f0, clamp(rough + widen, rough, 1.0), metal);
}


// The light a raindrop (a streak, a splash) at a render-space point sends toward the eye, per unit of its colour: a drop
// refracts and reflects what lies around it, so it shows the mean of the sky's and the ground's light (as a surface
// facing sideways takes them), and the sun's, the moon's, the local lights' (within a spot's cone) and a bolt's light
// falling on it spread over every direction.
vec3 RainDropLight(vec3 worldPos)
{
    SkyLightValues light = SkyLightAt(worldPos);
    vec3 around = (light.sky + light.ground)*(0.5/PI)*ambientTint + nightLight;
    vec3 falling = light.sun + boltLight;
    if (isMoonLit)
    {
        falling += light.moon;
    }
    int forwardLimit = min(forwardLightCount, MAX_FORWARD_LIGHTS);
    for (int i = 0; i < forwardLimit; i++)
    {
        vec3 toLight = forwardLightPositions[i] - worldPos;
        float dist = length(toLight);
        falling += forwardLightRadiances[i]*LocalLightFalloff(dist, forwardLightRanges[i])
            *LightCone(toLight/max(dist, 1e-4), forwardLightDirections[i], forwardLightCones[i]);
    }
    if (pointShadowActive)
    {
        falling += pointShadowLightRadiance*LocalLightFalloff(length(pointShadowLightPos - worldPos),
            pointShadowLightRange);
    }
    return around + falling*(0.25/PI);
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
