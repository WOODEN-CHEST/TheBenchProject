#version 330

// Full-screen atmospheric sky: Rayleigh + Mie single-scattering (shaders/common/atmosphere.glsl, which the PBR
// shader's distance fog shares), plus a sun disc and a night star field.
// Drawn behind the 3D geometry into the linear-HDR scene buffer; the tonemap post-pass maps it to display.
// The per-pixel world ray is reconstructed from the inverse view-projection so the sky lines up exactly with
// the 3D camera. Everything is world space with +Y up, matching WorldEnvironment_GetSunDirection.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform vec2 resolution;       // scene target size in pixels
uniform mat4 invViewProj;      // inverse(view * proj), matching BeginMode3D
uniform vec3 cameraPos;        // world-space camera position

uniform vec3 sunDirection;     // unit direction TO the sun (world, +Y up)
uniform vec3 sunColor;         // linear; tints the sun disc
uniform float sunIntensity;    // radiance multiplier
uniform float sunSize;         // angular-size multiplier for the sun disc
uniform float turbidity;       // atmospheric haze (default ~3)
uniform vec3 skyTint;          // linear multiplier, for alien skies

uniform float starSeed;        // seed offsetting the star field
uniform float starDensity;     // relative star density
uniform float starBrightness;  // star brightness multiplier

out vec4 finalColor;

#include "common/atmosphere"

float hash13(vec3 p)
{
    p = fract(p*0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y)*p.z);
}

// Sparse star field hashed directly from the 3D view ray, so stars are spread uniformly across the whole
// dome (the previous atan/asin mapping was singular at the zenith and clustered every star near the horizon).
// No horizon gate: stars cover the entire sphere so the sky reads as an infinite starfield.
vec3 stars(vec3 rayDir, float seed, float density, float brightness)
{
    vec3 cell = floor(rayDir*300.0 + seed);
    float present = hash13(cell);
    float threshold = 1.0 - clamp(density, 0.0, 1.0)*0.02;
    if (present < threshold) return vec3(0.0);
    float bright = hash13(cell + 3.7);
    return vec3(max(brightness, 0.0)*2.0*(0.4 + 0.6*bright));
}

void main()
{
    vec2 uv = gl_FragCoord.xy/resolution;
    vec4 clip = vec4(uv*2.0 - 1.0, 1.0, 1.0);
    vec4 world = invViewProj*clip;
    vec3 rayDir = normalize(world.xyz/world.w - cameraPos);

    // Ground viewer just above the planet surface; the sky dome uses world +Y as up.
    vec3 rayOrigin = vec3(0.0, ATMOSPHERE_R_PLANET + 1.0, 0.0);
    vec3 sunDir = normalize(sunDirection);

    float kMie = ATMOSPHERE_K_MIE_BASE*max(turbidity, 0.0)/3.0;
    float iSun = ATMOSPHERE_SUN_RADIANCE*max(sunIntensity, 0.0);

    // Infinite-skybox look: there is no ground. Compute the atmosphere with the ray mirrored into the upper
    // hemisphere (abs(y)) so every direction gets a sky colour, then darken below the horizon so it still
    // reads as "down" instead of a mirror-bright ground. The PBR shader's fogSkyColor repeats these steps.
    vec3 skyRay = vec3(rayDir.x, abs(rayDir.y), rayDir.z);
    vec3 sky = atmosphere(skyRay, rayOrigin, sunDir, iSun, kMie);
    float belowFactor = smoothstep(0.0, -0.12, rayDir.y); // 0 above the horizon, 1 below
    sky *= mix(1.0, 0.5, belowFactor);

    // Sun disc, only above the horizon (so the mirror does not create a second sun below).
    float discCos = cos(radians(0.5*max(sunSize, 0.01)));
    if ((rayDir.y > -0.02) && (dot(rayDir, sunDir) > discCos))
    {
        sky += max(sunColor, vec3(0.0))*iSun;
    }

    // Stars cover the whole dome (above and below the horizon), fading in as the sun drops below the horizon.
    float night = clamp(-sunDir.y*4.0, 0.0, 1.0);
    sky += stars(rayDir, starSeed, starDensity, starBrightness)*night;

    sky *= max(skyTint, vec3(0.0));
    finalColor = vec4(sky, 1.0); // linear HDR; tonemap post-pass maps to display
}
