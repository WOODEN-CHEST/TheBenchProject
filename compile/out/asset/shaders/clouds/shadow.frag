#version 330

// The clouds' shadow map (common/clouds): per texel, a point of a level plane at the low layer's base around the camera,
// the share of the cloud light (the sun, or the moon) that passes the layers toward the light: R through all of them, G
// the low layer's share, B the upper layers'. The lit shaders read it where a surface's ray toward the light crosses that
// plane (common/lighting's CloudShadowAt): a surface below the clouds takes all of it, one inside or above the low layer
// the low layer's part above itself and the upper layers'. Density only, without the erosion: the shadows are soft.
// The plane is level and the layers curve down with the planet away from the camera, so each layer is marched from
// where the light's ray through the plane's point reaches its base (behind the point where the planet has curved away
// below it: a low sun's map is centred tens of kilometres out).
// Drawn before the scene each frame (source/WorldRendererClouds.c).

in vec2 fragTexCoord;
in vec4 fragColor;

uniform vec2 resolution;          // the map's size in texels
uniform vec2 cloudShadowCenter;   // the map's centre on the plane, render space (x, z)
uniform float cloudShadowExtent;  // its side, metres
uniform float cloudShadowPlane;   // the plane's height in render space (the low layer's base minus the camera's height)
uniform vec3 cloudLightDirection; // the light the map is made for (unit, above the horizon)
uniform int cloudShadowSteps;     // the low layer's steps

out vec4 finalColor;

#include "common/clouds"

// The light's ray is no steeper than this (a low light's ray through the layer is capped, see CLOUD_SHADOW_MAX_PATH).
const float CLOUD_SHADOW_MIN_UP = 0.02;
// The longest stretch of the low layer the ray is marched through, metres.
const float CLOUD_SHADOW_MAX_PATH = 40000.0;

void main()
{
    vec2 uv = gl_FragCoord.xy/resolution;
    vec3 p = vec3(cloudShadowCenter.x + (uv.x - 0.5)*cloudShadowExtent, cloudShadowPlane,
        cloudShadowCenter.y + (uv.y - 0.5)*cloudShadowExtent);
    float lightUp = max(cloudLightDirection.y, CLOUD_SHADOW_MIN_UP);
    float pointHeight = cloudHeightAt(p); // the low layer's base near the camera, above it farther out
    float lowDepth = 0.0;
    if (cloudLowExtinction > 0.0)
    {
        float from = (cloudLowLayer.x - pointHeight)/lightUp;
        float path = min((cloudLowLayer.y - cloudLowLayer.x)/lightUp, CLOUD_SHADOW_MAX_PATH);
        float stepLength = path/float(cloudShadowSteps);
        for (int i = 0; i < cloudShadowSteps; i++)
        {
            vec3 q = p + cloudLightDirection*(from + (float(i) + 0.5)*stepLength);
            lowDepth += cloudLowDensity(q, cloudHeightAt(q), false);
        }
        lowDepth *= stepLength*cloudLowExtinction;
    }
    float upperDepth = 0.0;
    if (cloudMidExtinction > 0.0)
    {
        float from = (cloudMidLayer.x - pointHeight)/lightUp;
        float path = (cloudMidLayer.y - cloudMidLayer.x)/lightUp;
        float midDepth = 0.0;
        const int MID_STEPS = 4;
        for (int i = 0; i < MID_STEPS; i++)
        {
            vec3 q = p + cloudLightDirection*(from + (float(i) + 0.5)*path/float(MID_STEPS));
            midDepth += cloudMidDensity(q, cloudHeightAt(q));
        }
        upperDepth += midDepth*path/float(MID_STEPS)*cloudMidExtinction;
    }
    if (cloudHighLayer.w > 0.0)
    {
        vec3 q = p + cloudLightDirection*((cloudHighLayer.x - pointHeight)/lightUp);
        upperDepth += cloudHighDensity(q)*cloudHighLayer.w*min(1.0/lightUp, CLOUD_HIGH_MAX_SLANT);
    }
    float low = exp(-lowDepth);
    float upper = exp(-upperDepth);
    finalColor = vec4(low*upper, low, upper, 1.0);
}
