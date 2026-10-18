#version 330

// Full-screen atmospheric sky: Rayleigh + Mie single-scattering (shaders/common/atmosphere.glsl, which the PBR
// shader's distance fog shares), light pollution's skyglow, a sun disc and the Milky Way. The stars are a separate
// pass drawn over this one (shaders/stars).
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

uniform vec3 skyglowRadiance;      // light pollution's glow at the horizon, linear (0 by day)
uniform sampler2D milkyWayTexture; // sky_milky_way: an equirectangular panorama in galactic coordinates (sRGB)
uniform mat4 worldToGalactic;      // world direction -> galactic (x toward the galactic centre, z its north pole)
uniform float milkyWayRadiance;    // linear radiance of a white texel; 0 draws no Milky Way (and samples nothing)
uniform float milkyWayLod;         // mip level matching the pixel's angular size (explicit: see milkyWay)

out vec4 finalColor;

#include "common/atmosphere"
#include "common/night_sky"

// The Milky Way toward a world direction. The panorama is in galactic coordinates: the galactic centre in the middle,
// longitude growing to the left (the view from inside the sky, as astronomical all-sky maps show it) and the north
// galactic pole at the top (v = 0 is the image's top row as raylib loads it). The mip level is explicit because
// automatic derivatives jump where the longitude wraps around and would draw a one-pixel seam line.
vec3 milkyWay(vec3 rayDir)
{
    vec3 galactic = mat3(worldToGalactic)*rayDir;
    float longitude = atan(galactic.y, galactic.x);
    float latitude = asin(clamp(galactic.z, -1.0, 1.0));
    vec2 uv = vec2(0.5 - longitude/(2.0*PI), 0.5 - latitude/PI);
    vec3 texel = textureLod(milkyWayTexture, uv, milkyWayLod).rgb;
    return pow(max(texel, vec3(0.0)), vec3(2.2))*milkyWayRadiance; // sRGB -> linear
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

    // Infinite-skybox look: there is no ground. Compute the atmosphere (and the skyglow it scatters) with the ray
    // mirrored into the upper hemisphere (abs(y)) so every direction gets a sky colour, then darken below the horizon
    // so it still reads as "down" instead of a mirror-bright ground. The PBR shader's fogSkyColor repeats these steps.
    vec3 skyRay = vec3(rayDir.x, abs(rayDir.y), rayDir.z);
    vec3 sky = atmosphere(skyRay, rayOrigin, sunDir, iSun, kMie);
    sky += nightSkySkyglow(skyRay.y, skyglowRadiance);
    float belowFactor = smoothstep(0.0, -0.12, rayDir.y); // 0 above the horizon, 1 below
    sky *= mix(1.0, 0.5, belowFactor);

    // Sun disc, only above the horizon (so the mirror does not create a second sun below).
    float discCos = cos(radians(0.5*max(sunSize, 0.01)));
    if ((rayDir.y > -0.02) && (dot(rayDir, sunDir) > discCos))
    {
        sky += max(sunColor, vec3(0.0))*iSun;
    }

    // The Milky Way, above the horizon only, dimmed near it like the stars. milkyWayRadiance already holds its
    // brightness, the light pollution and the twilight.
    if ((milkyWayRadiance > 0.0) && (rayDir.y > 0.0))
    {
        sky += milkyWay(rayDir)*nightSkyExtinction(nightSkyAirmass(rayDir.y));
    }

    sky *= max(skyTint, vec3(0.0));
    finalColor = vec4(sky, 1.0); // linear HDR; tonemap post-pass maps to display
}
