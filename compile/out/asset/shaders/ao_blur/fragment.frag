#version 330

// The ambient pre-pass's blur: the raw pre-pass (ao/fragment, bound as the blit's texture) into the texture the lit
// passes read. R and G (the occlusion and the sky's visibility) are averaged over 5 x 5 pixels whose outer row and column
// weigh a half, which covers every cell of the pre-pass's 4 x 4 pattern exactly once at every pixel: the pattern is gone
// and the result stays centred. Only neighbours on the same surface count (their position near the centre's plane, their
// normal close to its), so edges stay crisp. B (the contact shadows, exact per pixel) is passed through.

in vec2 fragTexCoord;   // unused: sampling uses gl_FragCoord, as the other post passes do
in vec4 fragColor;

uniform sampler2D texture0;      // the raw pre-pass (R occlusion, G sky visibility, B contact light), the blit's source
uniform vec4 colDiffuse;
uniform sampler2D depthTexture;  // the G-buffer's samplable depth
uniform sampler2D normalTexture; // the normal/flag G-buffer (RGB view normal, A surface flag)
uniform vec2 resolution;         // the targets' size in pixels
uniform mat4 invProjection;      // inverse of the scene pass's projection
#include "common/depth"

out vec4 finalColor;

const float SURFACE_ON = 0.25;
const int BLUR_REACH = 2;                 // pixels each way: a 5 x 5 window
const float BLUR_PLANE_TOLERANCE = 0.02;  // how far off the centre's plane a neighbour may lie, per metre of distance
const float BLUR_NORMAL_MIN = 0.8;        // the least cosine between a neighbour's normal and the centre's

vec3 ViewPosAt(ivec2 pixel)
{
    float d = texelFetch(depthTexture, pixel, 0).r;
    vec2 uv = (vec2(pixel) + 0.5)/resolution;
    vec4 ndc = vec4(uv*2.0 - 1.0, depthToNdc(d), 1.0);
    vec4 view = invProjection*ndc;
    return view.xyz/view.w;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 raw = texelFetch(texture0, pixel, 0);
    vec4 gbuffer = texelFetch(normalTexture, pixel, 0);
    if (!(gbuffer.a > SURFACE_ON))
    {
        finalColor = raw; // the sky: nothing to blur
        return;
    }
    vec3 center = ViewPosAt(pixel);
    vec3 normal = normalize(gbuffer.rgb*2.0 - 1.0);
    float tolerance = BLUR_PLANE_TOLERANCE*max(-center.z, 1e-3);
    ivec2 last = ivec2(resolution) - 1;
    vec2 sum = vec2(0.0);
    float weightSum = 0.0;
    for (int dy = -BLUR_REACH; dy <= BLUR_REACH; dy++)
    {
        for (int dx = -BLUR_REACH; dx <= BLUR_REACH; dx++)
        {
            ivec2 neighbour = pixel + ivec2(dx, dy);
            if (any(lessThan(neighbour, ivec2(0))) || any(greaterThan(neighbour, last)))
            {
                continue;
            }
            vec4 neighbourGBuffer = texelFetch(normalTexture, neighbour, 0);
            if (!(neighbourGBuffer.a > SURFACE_ON)
                || (dot(normalize(neighbourGBuffer.rgb*2.0 - 1.0), normal) < BLUR_NORMAL_MIN)
                || (abs(dot(ViewPosAt(neighbour) - center, normal)) > tolerance))
            {
                continue;
            }
            float weight = ((abs(dx) == BLUR_REACH) ? 0.5 : 1.0)*((abs(dy) == BLUR_REACH) ? 0.5 : 1.0);
            sum += weight*texelFetch(texture0, neighbour, 0).rg;
            weightSum += weight;
        }
    }
    // The centre always counts (its weight is 1), so the sum is never empty.
    finalColor = vec4(sum/weightSum, raw.b, 1.0);
}
