// The rain on the world, for #include "common/rain" (common/lighting includes it through common/puddles, and the rain's
// own programs include it, in any stage): the rain map's lookups (what keeps the rain out), the rain's hash and the
// lightning bolt's light. The puddles, their ripples and the sky mirrored in water are common/puddles' (the fragment
// stage's: their mask's reads take screen derivatives). The pure maths and the constants' C twins are source/WorldRain.h
// (tests/ShaderSourceTests.c pins them).
//
// RENDER SPACE. Positions are relative to the camera.

#include "common/math"

// The rain map (WorldRain.h, "THE RAIN MAP"): a depth map along the rain's mean fall around the camera, of what keeps the
// sky out; a point behind its first surface, seen from where the rain comes from, is covered.
uniform bool rainMapActive;            // false: no map this frame (everything is open to the rain)
uniform sampler2DArrayShadow rainMap;  // its one layer, depth compared (bilinear)
uniform mat4 rainMapMatrix;            // render space -> the map's clip space
uniform vec2 rainMapOffsets;           // the lookups' depth bias (the map's depth) and their move off a surface (metres)

// The lightning bolt's channel: its light on a surface facing it, unshadowed (linear; 0: none), and the unit direction
// toward its middle.
uniform vec3 boltLight;
uniform vec3 boltDirection;

// The rain map's lookups fade to open over the outer part of its half size, from this share on
// (WORLD_RAIN_MAP_EDGE_FADE_START); a surface's four lookups lie this far off its point along two diagonals, metres.
const float RAIN_MAP_EDGE_FADE_START = 0.85;
const float RAIN_SURFACE_SPREAD = 0.16;
// Where the splashes' and the ripples' cycle counts wrap (WORLD_RAIN_CYCLE_WRAP): a cycle's number is taken modulo the
// same, so the wrap changes nothing.
const float RAIN_CYCLE_WRAP = 4096.0;

// The rain's hash of three whole numbers (WorldRain_Hash): three odd multipliers, then the "lowbias32" finalizer.
uint rainHash(uvec3 v)
{
    uint h = (v.x*0x8DA6B343u) ^ (v.y*0xD8163841u) ^ (v.z*0xCB1AB31Fu);
    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    h *= 0x846CA68Bu;
    h ^= h >> 16u;
    return h;
}

// A hash's two halves as two numbers in [0, 1].
vec2 rainHashPair(uint h)
{
    return vec2(float(h & 0xFFFFu), float(h >> 16u))/65535.0;
}

// How open a point is to the rain from its place in the map's clip space (orthographic: w is 1), 0 covered .. 1 open:
// compared with the map's first surface (bilinear over four texels), fading to open toward the map's sides; open at its
// far plane and beyond, farther down the rain than its depth reaches (the float depth map does not clamp the comparison:
// everything there would read as covered) (WorldRain_GetMapEdgeOpenness).
float RainOpennessOfClip(vec3 clip)
{
    float edge = smoothstep(RAIN_MAP_EDGE_FADE_START, 1.0, max(abs(clip.x), abs(clip.y)));
    if (!(edge < 1.0) || !(clip.z < 1.0))
    {
        return 1.0;
    }
    vec3 coordinate = clip*0.5 + 0.5;
    float open = texture(rainMap, vec4(coordinate.xy, 0.0, coordinate.z - rainMapOffsets.x));
    return mix(open, 1.0, edge);
}

// How open a render-space point is to the rain, 0 covered .. 1 open; open without a map.
float RainOpennessAt(vec3 p)
{
    if (!rainMapActive)
    {
        return 1.0;
    }
    return RainOpennessOfClip((rainMapMatrix*vec4(p, 1.0)).xyz);
}

// A surface's openness: its lookup moved off it along its geometric normal (unit), so no surface covers itself through
// the map's texels at a slant, and spread over four lookups RAIN_SURFACE_SPREAD apart along two diagonals across it (a
// dry patch's edge is soft: spray and run-off blur it). The map is orthographic, so the offsets move the place in its
// clip space by fixed amounts.
float RainOpenness(vec3 p, vec3 surfaceNormal)
{
    if (!rainMapActive)
    {
        return 1.0;
    }
    vec3 helper = (abs(surfaceNormal.y) < 0.9) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 across = normalize(cross(surfaceNormal, helper));
    vec3 along = cross(surfaceNormal, across);
    vec3 at = (rainMapMatrix*vec4(p + surfaceNormal*rainMapOffsets.y, 1.0)).xyz;
    vec3 first = (rainMapMatrix*vec4((across + along)*RAIN_SURFACE_SPREAD, 0.0)).xyz;
    vec3 second = (rainMapMatrix*vec4((across - along)*RAIN_SURFACE_SPREAD, 0.0)).xyz;
    return 0.25*(RainOpennessOfClip(at + first) + RainOpennessOfClip(at + second) + RainOpennessOfClip(at - first)
        + RainOpennessOfClip(at - second));
}
