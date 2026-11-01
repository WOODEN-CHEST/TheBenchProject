#version 330

// The clouds' accumulation over frames: this frame's march (its steps jittered anew every frame) blended into the last
// frame's result, reprojected to where the clouds stood then, so the march's noise averages out over a few frames. The
// last result is clamped to what this frame's march shows around the texel (its neighbours' range), so clouds that moved
// or changed leave no trails. The sky's texels and the geometry's (alpha below 0: -1 - the transmittance) are kept
// apart: each takes its range and its history from its own kind only.
// Drawn after the march (source/WorldRendererClouds.c); the sky pass and the stars read the result.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D currentClouds;      // this frame's march (RGB what the clouds add, A their transmittance; the geometry's
                                      // texels -1 - it)
uniform sampler2D historyClouds;      // the last frame's result, the same size
uniform vec2 resolution;              // the targets' size in texels
uniform mat4 invViewProj;             // this frame's clip -> render space
uniform mat4 previousViewProjection;  // the last frame's render space -> clip
uniform vec3 originShift;             // this frame's origin minus the last frame's, metres
uniform bool hasHistory;              // false: no last result (the first frame, a new target, a jump)
uniform float currentShare;           // the share of this frame's march in the result

out vec4 finalColor;

#include "common/clouds"

// Where the clouds lie along a ray, for the reprojection: the first layer it meets, at least a little away from the
// camera (from inside a layer); far for a ray that meets none.
const float CLOUD_REPROJECTION_NEAREST = 300.0;
const float CLOUD_REPROJECTION_FARTHEST = 20000.0;

float cloudDistanceAlong(vec3 direction)
{
    float best = CLOUD_REPROJECTION_FARTHEST;
    float start;
    float end;
    if ((cloudLowExtinction > 0.0) && cloudShellSegment(direction, cloudLowLayer.x, cloudLowLayer.y, start, end))
    {
        best = min(best, start);
    }
    if ((cloudMidExtinction > 0.0) && cloudShellSegment(direction, cloudMidLayer.x, cloudMidLayer.y, start, end))
    {
        best = min(best, start);
    }
    // The high layer's crossing: the far one from below, the near one from above.
    if ((cloudHighLayer.w > 0.0) && cloudSphereDistances(direction.y, cloudHighLayer.x, start, end))
    {
        float crossing = (atmosphereViewHeight < cloudHighLayer.x) ? end : start;
        best = (crossing > 0.0) ? min(best, crossing) : best;
    }
    return max(best, CLOUD_REPROJECTION_NEAREST);
}

// A stored texel as what the clouds add and their transmittance, whichever kind it is.
vec4 cloudDecoded(vec4 stored)
{
    return (stored.a < 0.0) ? vec4(stored.rgb, -1.0 - stored.a) : stored;
}

// The last result at a place on its target, its texels of the kind asked for around it weighed bilinearly (decoded);
// false when none is of that kind.
bool historyAt(vec2 uv, bool isGeometry, out vec4 history)
{
    vec2 position = uv*resolution - 0.5;
    vec2 base = floor(position);
    vec2 share = position - base;
    ivec2 last = ivec2(resolution) - 1;
    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    for (int corner = 0; corner < 4; corner++)
    {
        ivec2 offset = ivec2(corner & 1, corner >> 1);
        vec4 value = texelFetch(historyClouds, clamp(ivec2(base) + offset, ivec2(0), last), 0);
        float weight = ((offset.x == 1) ? share.x : 1.0 - share.x)*((offset.y == 1) ? share.y : 1.0 - share.y);
        if (((value.a < 0.0) == isGeometry) && (weight > 0.0))
        {
            sum += cloudDecoded(value)*weight;
            weightSum += weight;
        }
    }
    history = (weightSum > 0.0) ? sum/weightSum : vec4(0.0);
    return weightSum > 0.0;
}

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec4 stored = texelFetch(currentClouds, texel, 0);
    bool isGeometry = stored.a < 0.0;
    // No history to take, or nothing in front of a surface (no clouds there: nothing to average).
    if (!hasHistory || (stored.a <= -2.0))
    {
        finalColor = stored;
        return;
    }
    vec4 current = cloudDecoded(stored);
    // This frame's range around the texel, among the texels of its kind.
    vec4 low = current;
    vec4 high = current;
    ivec2 last = ivec2(resolution) - 1;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec4 neighbour = texelFetch(currentClouds, clamp(texel + ivec2(x, y), ivec2(0), last), 0);
            if ((neighbour.a < 0.0) == isGeometry)
            {
                vec4 value = cloudDecoded(neighbour);
                low = min(low, value);
                high = max(high, value);
            }
        }
    }
    // Where this texel's clouds stood on the last frame's screen.
    vec2 uv = gl_FragCoord.xy/resolution;
    vec4 world = invViewProj*vec4(uv*2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(world.xyz/world.w);
    vec4 clip = previousViewProjection*vec4(direction*cloudDistanceAlong(direction) + originShift, 1.0);
    vec2 previousUv = clip.xy/max(clip.w, 1e-6)*0.5 + 0.5;
    vec4 history;
    if ((clip.w <= 0.0) || any(lessThan(previousUv, vec2(0.0))) || any(greaterThan(previousUv, vec2(1.0)))
        || !historyAt(previousUv, isGeometry, history))
    {
        finalColor = stored;
        return;
    }
    vec4 blended = mix(clamp(history, low, high), current, currentShare);
    finalColor = isGeometry ? vec4(blended.rgb, -1.0 - blended.a) : blended;
}
