#version 330

// The clouds in front of the scene's surfaces: over every pixel where the G-buffer holds a surface, the clouds the march
// found between the camera and it (the cloud target's texels of the geometry, A = -1 - their transmittance) laid over the
// scene's colour by the blend: scene x the transmittance + the light they add. The sky's pixels took their clouds in the
// sky pass, and get nothing here (A 1). Drawn after the scene and its outlines, before bloom, the sun shafts and the eye
// (source/WorldRendererClouds.c).

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D cloudLayer;      // the clouds' result: RGB what they add, A their transmittance (the geometry's: -1 - it)
uniform vec2 cloudLayerResolution; // its size in texels
uniform sampler2D sceneDepth;      // the G-buffer's depth, at this pass's size
uniform vec2 resolution;           // this pass's size in pixels (the scene's)

out vec4 finalColor;

#include "common/depth"

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (depthIsSky(texelFetch(sceneDepth, pixel, 0).r))
    {
        finalColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // The geometry's texels around the pixel, weighed bilinearly (one texel when the target is the scene's size); the
    // sky's texels left out.
    vec2 position = gl_FragCoord.xy/resolution*cloudLayerResolution - 0.5;
    vec2 base = floor(position);
    vec2 share = position - base;
    ivec2 last = ivec2(cloudLayerResolution) - 1;
    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    for (int corner = 0; corner < 4; corner++)
    {
        ivec2 offset = ivec2(corner & 1, corner >> 1);
        vec4 value = texelFetch(cloudLayer, clamp(ivec2(base) + offset, ivec2(0), last), 0);
        float weight = ((offset.x == 1) ? share.x : 1.0 - share.x)*((offset.y == 1) ? share.y : 1.0 - share.y);
        if ((value.a < 0.0) && (weight > 0.0))
        {
            sum += vec4(value.rgb, -1.0 - value.a)*weight;
            weightSum += weight;
        }
    }
    finalColor = (weightSum > 0.0) ? sum/weightSum : vec4(0.0, 0.0, 0.0, 1.0);
}
