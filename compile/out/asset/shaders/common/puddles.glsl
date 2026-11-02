// Water on wet surfaces, for #include "common/puddles" (common/lighting includes it; a fragment stage's: the puddle mask's
// reads take screen derivatives): the puddles' mask and threshold, their ripples, the sky mirrored in water and the rain's
// debug view. What keeps the rain out is common/rain's. The pure maths and the constants' C twins are source/WorldRain.h
// (tests/ShaderSourceTests.c pins them).
//
// RENDER SPACE. Positions are relative to the camera. The patterns anchored to the world (the puddle mask, the ripples)
// get the camera's position wrapped by their periods, in double, from the renderer.

#include "common/math"
#include "common/atmosphere"
#include "common/rain"

// The puddles and their rings (WorldRain.h, "PUDDLES").
uniform bool rainPuddleActive;         // false: no puddles this frame (dry, no coverage, or no mask)
uniform sampler2D puddleMask;          // red: where water pools first, spread evenly over 0 .. 1, tileable
uniform vec4 rainPuddleFrame;          // xy: the origin wrapped by the mask's pattern, z: 1 / the mask's tile, w: the
                                       // coverage of a surface whose puddles is 1
uniform vec4 rainRippleFrame;          // xy: the origin wrapped by the ripples' period, z: how many cycles they have run
                                       // (WorldRainDrift.RippleCycles, wrapped by RAIN_CYCLE_WRAP), w: their strength
                                       // (0: still water)

// The sky mirrored in water (none while the rain is switched off: common/rain's rainActive): the sky view's clear sky
// (common/atmosphere's table, on its own unit in the lit programs),
// the share of it the reflection takes (the gaps between drawn clouds; 1 without them, when the table holds what the
// clouds leave of it), and the clouds' deck on top (an overcast sky's radiance at the zenith, falling to a third at the
// horizon; tinted, a lightning flash's glow in).
uniform float rainReflectionClearShare;
uniform vec3 rainReflectionDeck;
uniform bool rainReflectionMoon;       // the moonlit sky's half of the table adds to it

// The debug view: 0 off, 1 how open every lit surface is to the rain (blue covered, white open), 2 the puddles (cyan; a
// wet film dark blue).
uniform int rainView;

// The puddles: surfaces facing up more than RAIN_PUDDLE_FLAT_START (the normal's y) start to pool, fully from
// RAIN_PUDDLE_FLAT_END; their rim spans this much of the mask's range.
const float RAIN_PUDDLE_FLAT_START = 0.85;
const float RAIN_PUDDLE_FLAT_END = 0.95;
const float RAIN_PUDDLE_EDGE = 0.04;
// The mask's second read: turned by cosine 4/5 and sine 3/5 and 5/4 as fine, so the two repeat together only every four
// tiles (WORLD_RAIN_PATTERN_TILES).
const vec2 RAIN_PUDDLE_TURN = vec2(0.8, 0.6);
const float RAIN_PUDDLE_TURNED_SCALE = 1.25;
// The ripples: their cells (WORLD_RAIN_RIPPLE_CELL), how many of them the wrapped origin's period holds
// (WORLD_RAIN_RIPPLE_PERIOD / WORLD_RAIN_RIPPLE_CELL), how far a ring spreads before it is gone, its width and its slope.
const float RAIN_RIPPLE_CELL = 0.25;
const float RAIN_RIPPLE_CELLS = 256.0;
const float RAIN_RIPPLE_RADIUS = 0.3;
const float RAIN_RIPPLE_WIDTH = 0.025;
const float RAIN_RIPPLE_SLOPE = 0.35;
// Water: its reflectance head-on.
const float RAIN_WATER_F0 = 0.02;

// The puddle mask's threshold for a coverage (WorldRain_GetPuddleThreshold): the mean of two even reads passes it on that
// share of a surface.
float RainPuddleThreshold(float coverage)
{
    float c = clamp(coverage, 0.0, 1.0);
    return (c <= 0.5) ? (1.0 - sqrt(0.5*c)) : sqrt(0.5*(1.0 - c));
}

// Where the puddle mask lies at a render-space point: its coordinates (in tiles) and their screen derivatives, taken in
// uniform control flow so the mask's mipmapped reads stay defined wherever they are made.
struct RainPuddlePlace
{
    vec2 at;
    vec2 dx;
    vec2 dy;
};

RainPuddlePlace RainPuddlePlaceAt(vec3 p)
{
    RainPuddlePlace place;
    place.at = (p.xz + rainPuddleFrame.xy)*rainPuddleFrame.z;
    place.dx = dFdx(place.at);
    place.dy = dFdy(place.at);
    return place;
}

// A coordinate turned by the mask's second read's turn and scale.
vec2 RainPuddleTurned(vec2 at)
{
    return vec2(RAIN_PUDDLE_TURN.x*at.x - RAIN_PUDDLE_TURN.y*at.y, RAIN_PUDDLE_TURN.y*at.x + RAIN_PUDDLE_TURN.x*at.y)
        *RAIN_PUDDLE_TURNED_SCALE;
}

// The share of a surface under a puddle: where the mask's two reads' mean passes the threshold for its coverage (the
// frame's times the surface's own puddles and its flatness), with a soft rim; 0 for surfaces not facing up.
float RainPuddleShare(RainPuddlePlace place, vec3 surfaceNormal, float puddles)
{
    float coverage = rainPuddleFrame.w*clamp(puddles, 0.0, 1.0)
        *smoothstep(RAIN_PUDDLE_FLAT_START, RAIN_PUDDLE_FLAT_END, surfaceNormal.y);
    if (!rainPuddleActive || !(coverage > 0.0))
    {
        return 0.0;
    }
    float first = textureGrad(puddleMask, place.at, place.dx, place.dy).r;
    float second = textureGrad(puddleMask, RainPuddleTurned(place.at), RainPuddleTurned(place.dx),
        RainPuddleTurned(place.dy)).r;
    float threshold = RainPuddleThreshold(coverage);
    return smoothstep(threshold - RAIN_PUDDLE_EDGE, threshold + RAIN_PUDDLE_EDGE, 0.5*(first + second));
}

// The slope (dh/dx, dh/dz) of the rain's rings on water at a render-space point: in each of the ripple cells around it
// one ring at a time, at a random place, starting again every cycle (WorldRainFrame.RippleRate of them a second) at a
// random phase: a wave packet that spreads out and fades.
vec2 RainRippleSlope(vec3 p)
{
    if (!(rainRippleFrame.w > 0.0))
    {
        return vec2(0.0);
    }
    vec2 at = (p.xz + rainRippleFrame.xy)/RAIN_RIPPLE_CELL;
    vec2 home = floor(at);
    vec2 slope = vec2(0.0);
    for (int dz = -1; dz <= 1; dz++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            vec2 cell = home + vec2(float(dx), float(dz));
            uvec2 whole = uvec2(mod(cell, RAIN_RIPPLE_CELLS));
            float phase = rainHashPair(rainHash(uvec3(whole, 1u))).x;
            float cycle = rainRippleFrame.z + phase;
            float age = fract(cycle);
            vec2 place = cell + 0.2
                + 0.6*rainHashPair(rainHash(uvec3(whole, uint(mod(floor(cycle), RAIN_CYCLE_WRAP)) + 2u)));
            vec2 offset = (at - place)*RAIN_RIPPLE_CELL;
            float r = length(offset);
            float x = (r - age*RAIN_RIPPLE_RADIUS)/RAIN_RIPPLE_WIDTH;
            float fade = (1.0 - age)*(1.0 - age);
            float dhdr = exp(-x*x)*cos(3.0*x)*fade;
            slope += dhdr*offset/max(r, 1e-4);
        }
    }
    return slope*RAIN_RIPPLE_SLOPE*rainRippleFrame.w;
}

// Water's normal on a puddle: straight up, rippled while it rains.
vec3 RainWaterNormal(vec3 p)
{
    vec2 slope = RainRippleSlope(p);
    return normalize(vec3(-slope.x, 1.0, -slope.y));
}

// The sky's light toward an upward direction r (unit) as water mirrors it: the sky view's clear sky (the gaps' share of it)
// and the clouds' deck; without the tables, the even sky evenSky (the sky's irradiance / pi).
vec3 RainSkyRadiance(vec3 r, vec3 sunDir, vec3 moonDir, vec3 evenSky)
{
    if (!atmosphereTablesActive)
    {
        return evenSky;
    }
    vec3 sky = atmosphereSkyView(r, sunDir, 0.0);
    if (rainReflectionMoon)
    {
        sky += atmosphereSkyView(r, moonDir, 1.0);
    }
    return sky*rainReflectionClearShare + rainReflectionDeck*((1.0 + 2.0*r.y)/3.0);
}

// The sky mirrored by the share `water` of a surface (normal n, unit; v toward the eye): water's Fresnel reflectance
// times the sky's light toward the reflected direction, kept above the horizon (a ripple's tilt would otherwise mirror the
// ground the sky view holds below it).
vec3 RainWaterReflection(vec3 n, vec3 v, float water, vec3 sunDir, vec3 moonDir, vec3 evenSky)
{
    if (!rainActive || !(water > 0.0))
    {
        return vec3(0.0);
    }
    vec3 r = reflect(-v, n);
    r = normalize(vec3(r.x, max(r.y, 0.02), r.z));
    float facing = 1.0 - clamp(dot(n, v), 0.0, 1.0);
    float fresnel = RAIN_WATER_F0 + (1.0 - RAIN_WATER_F0)*facing*facing*facing*facing*facing;
    return RainSkyRadiance(r, sunDir, moonDir, evenSky)*fresnel*water;
}

// The debug view over a lit surface's colour: how open it is to the rain, or its water (`puddle`: the share mirroring the
// sky): cyan where it pools, a dark blue where only a wet film lies, the dry rest darkened.
vec3 RainViewColor(vec3 color, vec3 p, vec3 surfaceNormal, float puddle)
{
    if (rainView == 1)
    {
        float open = RainOpenness(p, surfaceNormal)*RainTopOpenness(p.y);
        return mix(vec3(0.05, 0.1, 0.6), vec3(1.0), open);
    }
    if (rainView == 2)
    {
        float pooled = smoothstep(0.5, 0.95, puddle);
        return mix(mix(color*0.2, vec3(0.15, 0.25, 0.45), clamp(puddle*2.0, 0.0, 1.0)), vec3(0.1, 0.9, 0.9), pooled);
    }
    return color;
}
