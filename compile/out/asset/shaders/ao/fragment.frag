#version 330

// Screen-space ambient occlusion, computed as a PRE-pass (before the scene colour pass) from the low-res
// normal/flag G-buffer's samplable depth + surface flag. The result is an ambient MULTIPLIER (1 = fully open,
// < 1 = occluded) that the PBR scene pass samples and applies to ONLY its ambient term. This replaces the old
// approach of multiplying AO onto the final scene colour in world_postfx, which also dimmed the direct sun /
// point-light contribution (AO should darken indirect/ambient light only). All sampling uses gl_FragCoord for
// orientation-consistency with the G-buffer, matching the other post passes.

in vec2 fragTexCoord;   // unused (see the gl_FragCoord note); kept so the blit has an output
in vec4 fragColor;

uniform sampler2D texture0;      // the G-buffer colour, bound by the blit; unused here but needed for the draw
uniform vec4 colDiffuse;
uniform sampler2D depthTexture;  // the G-buffer's samplable depth (same geometry + projection as the scene pass)
uniform sampler2D normalTexture; // the normal/flag G-buffer (A = surface flag: 0 = sky/grid, >= 0.5 = surface)

uniform vec2 resolution;         // G-buffer size in pixels
uniform mat4 invProjection;      // inverse of the perspective projection used by the scene pass
uniform mat4 projection;         // the perspective projection used by the scene pass (for sample re-projection)
uniform float aoStrength;        // effective AO strength (config x world); 0 disables (output stays 1 = open)

out vec4 finalColor;

const float SURFACE_ON = 0.25;   // G-buffer alpha above this = a solid world-model surface (0 = sky / grid)

// ---- SSAO tuning (kept identical to the values the postfx pass used before AO moved here) ----
const int   AO_KERNEL_SIZE = 12;
const float AO_RADIUS = 0.5;     // hemisphere radius, view-space units
const float AO_TANGENT_BIAS = 0.02; // occluder must rise this far above the tangent plane to start counting
const float AO_TANGENT_FULL = 0.10; // ... and this far to count fully (so flat/tilted planes self-occlude ~0)
const float AO_MAX = 0.6;        // clamp so AO never fully blackens the ambient term

const vec3 AO_KERNEL[12] = vec3[12](
    vec3( 0.50,  0.00,  0.50), vec3(-0.40,  0.30,  0.40),
    vec3( 0.20, -0.50,  0.30), vec3(-0.30, -0.20,  0.60),
    vec3( 0.60,  0.40,  0.20), vec3(-0.50, -0.40,  0.30),
    vec3( 0.10,  0.60,  0.50), vec3( 0.35, -0.15,  0.70),
    vec3(-0.15,  0.10,  0.25), vec3( 0.00, -0.35,  0.20),
    vec3(-0.25,  0.45,  0.55), vec3( 0.45, -0.45,  0.25)
);

// Reconstructs the view-space position of the G-buffer pixel at uv from its stored depth.
vec3 ViewPosFromUv(vec2 uv)
{
    float d = texture(depthTexture, uv).r;
    vec4 ndc = vec4(uv*2.0 - 1.0, d*2.0 - 1.0, 1.0);
    vec4 view = invProjection*ndc;
    return view.xyz/view.w;
}

// Stable orthonormal basis around a +z-ish normal (Duff et al., branchless); no randomness -> no AO speckle.
mat3 TangentBasis(vec3 normal)
{
    float a = -1.0/(1.0 + normal.z);
    float b = normal.x*normal.y*a;
    vec3 tangent = vec3(1.0 + normal.x*normal.x*a, b, -normal.x);
    vec3 bitangent = vec3(b, 1.0 + normal.y*normal.y*a, -normal.y);
    return mat3(tangent, bitangent, normal);
}

// Tangent-plane hemisphere SSAO: an occluder counts only insofar as it stands above this surface's tangent
// plane, so flat/tilted planes self-occlude ~0 (no grazing-angle dark ground). Returns occlusion in [0,1].
float ComputeOcclusion(vec3 viewPos, vec3 normal)
{
    mat3 tbn = TangentBasis(normal);

    float occlusion = 0.0;
    for (int i = 0; i < AO_KERNEL_SIZE; i++)
    {
        float scale = float(i)/float(AO_KERNEL_SIZE);
        scale = mix(0.1, 1.0, scale*scale);
        vec3 samplePos = viewPos + (tbn*(normalize(AO_KERNEL[i])*scale))*AO_RADIUS;

        vec4 offset = projection*vec4(samplePos, 1.0);
        vec2 sampleUv = (offset.xy/offset.w)*0.5 + 0.5;
        if ((sampleUv.x < 0.0) || (sampleUv.x > 1.0) || (sampleUv.y < 0.0) || (sampleUv.y > 1.0))
        {
            continue;
        }
        // Only solid world geometry occludes (sky/grid have surface flag 0).
        if (texture(normalTexture, sampleUv).a < SURFACE_ON)
        {
            continue;
        }

        vec3 occluder = ViewPosFromUv(sampleUv);
        vec3 diff = occluder - viewPos;
        float aboveTangent = smoothstep(AO_TANGENT_BIAS, AO_TANGENT_FULL, dot(diff, normal));
        float rangeCheck = smoothstep(0.0, 1.0, AO_RADIUS/max(length(diff), 1e-4));
        occlusion += aboveTangent*rangeCheck;
    }
    return occlusion/float(AO_KERNEL_SIZE);
}

void main()
{
    vec2 uv = gl_FragCoord.xy/resolution;
    vec4 gbuffer = texture(normalTexture, uv);
    bool isSurface = gbuffer.a > SURFACE_ON;

    float aoMul = 1.0; // sky / grid / no-AO => fully open (the PBR ambient is unchanged there)
    if ((aoStrength > 0.0) && isSurface)
    {
        vec3 viewPos = ViewPosFromUv(uv);
        vec3 geoNormal = normalize(cross(dFdx(viewPos), dFdy(viewPos)));
        if (geoNormal.z < 0.0) { geoNormal = -geoNormal; }
        float occlusion = ComputeOcclusion(viewPos, geoNormal);
        aoMul = 1.0 - clamp(occlusion*aoStrength, 0.0, AO_MAX);
    }

    finalColor = vec4(vec3(aoMul), 1.0);
}
