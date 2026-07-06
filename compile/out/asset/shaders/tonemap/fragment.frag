#version 330

// Full-screen tonemap post-pass: samples the linear-HDR scene texture and maps it to displayable sRGB.
// Runs during the scene->frame blit (after the pixelation upscale), so the whole scene is composited in HDR
// and only mapped to [0,1] for display. Point filtering on the source keeps the pixels chunky.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   // the linear-HDR scene render target
uniform vec4 colDiffuse;      // blit tint (WHITE in normal use)
uniform float exposure;       // HDR eye-adaptation multiplier (1 = neutral); set by the renderer each frame

// Bloom: a blurred bright-pass of the scene, added back in linear HDR before tonemapping. bloomStrength 0
// disables it (and the sampler is then NOT read, so it need not be bound — avoids reading unit 0 by mistake).
uniform sampler2D bloomTexture;
uniform float bloomStrength;

// Sun shafts (god rays): same idea — added in linear HDR, and only sampled when its strength is > 0.
uniform sampler2D sunshaftTexture;
uniform float sunshaftStrength;

out vec4 finalColor;

// Narkowicz ACES filmic tonemapping approximation (maps linear HDR to displayable 0..1).
vec3 TonemapACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x + b))/(x*(c*x + d) + e), 0.0, 1.0);
}

void main()
{
    vec4 scene = texture(texture0, fragTexCoord);
    vec3 hdr = max(scene.rgb, vec3(0.0));

    // Add bloom (already the blurred bright-pass) in linear HDR. Only sample when it actually contributes, so a
    // disabled/unbound bloom sampler is never read (an unbound sampler would read texture unit 0 = the scene).
    if (bloomStrength > 0.0)
    {
        hdr += max(texture(bloomTexture, fragTexCoord).rgb, vec3(0.0)) * bloomStrength;
    }
    if (sunshaftStrength > 0.0)
    {
        hdr += max(texture(sunshaftTexture, fragTexCoord).rgb, vec3(0.0)) * sunshaftStrength;
    }

    // Apply eye-adaptation exposure in linear HDR, then tonemap. exposure defaults to 0 if the renderer never
    // set it (uninitialized uniform); guard so the image is not blacked out in that case.
    float ev = (exposure > 0.0) ? exposure : 1.0;
    vec3 mapped = TonemapACES(hdr*ev);
    mapped = pow(mapped, vec3(1.0/2.2)); // linear -> sRGB for the 8-bit frame target

    finalColor = vec4(mapped, scene.a)*fragColor*colDiffuse;
}
