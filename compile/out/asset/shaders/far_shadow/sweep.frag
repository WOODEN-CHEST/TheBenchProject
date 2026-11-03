#version 330

// The far shadows' sweep (world_far_shadow_sweep; source/WorldFarShadows.h, whose C mirror WorldFarShadows_Sweep is): one
// pass over one map, a texel per fragment. For every texel x, the height below which x lies in the terrain's shadow for
// the light's disc's top and bottom edges (R, G): S(x) = max over t >= t0 of h(x + t d) - t s, d the light's horizontal
// direction, s an edge's slope, t0 the exclusion (the casters nearer are the shadow levels'). The first pass takes one
// sample, at t0, from the heights, and continues a ray that leaves the map in the next coarser map's finished sweep (from
// where it leaves, or from t0 if sooner: that map's value read at the exit pulled back by its own exclusion is its S from
// the exit itself); pass k takes the pass before's at x and at x moved 2^(k-1) texels along the light, lowered by that
// run's climb, so their max covers twice the run. Samples past the map's edge are no ground. In copy mode the pass copies
// the source into the maps the lit shaders read.

uniform sampler2DArray farSweepHeights;  // the heights, a layer per map (the first pass)
uniform sampler2DArray farSweepSource;   // the pass before's shadow heights, layer 0 (every other pass, and the copies)
uniform sampler2DArray farSweepCoarser;  // the next coarser map's finished shadow heights, layer 0 (the first pass)
uniform int farSweepLayer;               // this map's layer of the heights
uniform bool farSweepIsFirst;
uniform bool farSweepIsCopy;
uniform bool farSweepHasCoarser;
uniform float farSweepSize;              // the map's texels a side
uniform vec2 farSweepToward;             // the light's unit horizontal direction (the world's x, z: the map's u, v)
uniform vec2 farSweepSlopes;             // the slopes of the light's disc's top and bottom edges
uniform float farSweepWidth;             // the map's side, metres
uniform float farSweepRun;               // the run along the light this pass's sample lies at, metres
uniform float farSweepCoarserWidth;      // the coarser map's side and its exclusion, metres
uniform float farSweepCoarserExclusion;

out vec4 finalColor;

// A texel no terrain covers, and no ray's sample (WORLD_FAR_SHADOW_NO_GROUND).
const float FAR_SWEEP_NO_GROUND = -1.0e9;

bool IsOnMap(vec2 uv)
{
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}

// How far, in uv, a ray from uv along toward runs before it leaves the map.
float ExitRun(vec2 uv, vec2 toward)
{
    float runX = (toward.x > 0.0) ? ((1.0 - uv.x)/toward.x) : ((toward.x < 0.0) ? (uv.x/-toward.x) : 1.0e30);
    float runY = (toward.y > 0.0) ? ((1.0 - uv.y)/toward.y) : ((toward.y < 0.0) ? (uv.y/-toward.y) : 1.0e30);
    return max(min(runX, runY), 0.0);
}

void main()
{
    ivec3 texel = ivec3(ivec2(gl_FragCoord.xy), 0);
    if (farSweepIsCopy)
    {
        finalColor = vec4(texelFetch(farSweepSource, texel, 0).rg, 0.0, 1.0);
        return;
    }
    vec2 uv = gl_FragCoord.xy/farSweepSize;
    vec2 sampleUv = uv + farSweepToward*(farSweepRun/farSweepWidth);
    vec2 shadow = vec2(FAR_SWEEP_NO_GROUND);
    if (IsOnMap(sampleUv))
    {
        shadow = farSweepIsFirst ? vec2(texture(farSweepHeights, vec3(sampleUv, float(farSweepLayer))).r)
            : texture(farSweepSource, vec3(sampleUv, 0.0)).rg;
        shadow -= farSweepRun*farSweepSlopes;
    }
    if (farSweepIsFirst && farSweepHasCoarser)
    {
        float leave = max(ExitRun(uv, farSweepToward)*farSweepWidth, farSweepRun);
        vec2 exitUv = uv + farSweepToward*(leave/farSweepWidth);
        vec2 coarserUv = (exitUv - 0.5)*(farSweepWidth/farSweepCoarserWidth) + 0.5
            - farSweepToward*(farSweepCoarserExclusion/farSweepCoarserWidth);
        shadow = max(shadow, texture(farSweepCoarser, vec3(coarserUv, 0.0)).rg
            + (farSweepCoarserExclusion - leave)*farSweepSlopes);
    }
    if (!farSweepIsFirst)
    {
        shadow = max(shadow, texelFetch(farSweepSource, texel, 0).rg);
    }
    finalColor = vec4(shadow, 0.0, 1.0);
}
