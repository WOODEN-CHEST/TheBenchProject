#version 330

// Full-screen atmospheric sky: Rayleigh + Mie single-scattering (shaders/common/atmosphere.glsl, which the PBR
// shader's distance fog shares) lit by the sun and, at night, by the moon; light pollution's skyglow; a sun disc, the
// moon's disc and the Milky Way. The eye's night vision (shaders/common/night_vision.glsl) greys the dim sky, not the
// discs. The stars are a separate pass drawn over this one (shaders/stars).
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

uniform vec3 moonDirection;        // unit direction TO the moon (world, +Y up)
uniform float moonSkyIntensity;    // the moon's light on the atmosphere relative to the sun's; 0 skips the moonlit sky
uniform float moonOpacity;         // the disc's opacity (it fades out near the sun); 0 draws no disc
uniform vec3 moonRight;            // the disc's axes on the sky, perpendicular to moonDirection: right, and up toward
uniform vec3 moonUp;               //   the sky's pole (so the face turns as the moon crosses the sky)
uniform float moonCosRadius;       // cosine and sine of the disc's angular radius
uniform float moonSinRadius;
uniform vec3 moonColor;            // linear tint of the disc (the moonlight's colour)
uniform float moonDiscRadiance;    // linear radiance of a white texel lit head-on
uniform float moonEarthshine;      // the dark side's glow as a share of that (already x (1 - illumination))
uniform bool hasMoonTexture;       // false: the disc is a flat grey
uniform sampler2D moonTexture;     // sky_moon: the full moon's face, flat-lit (sRGB; alpha 0 outside the disc)

uniform float nightVisionFieldLuminance; // the scene luminance the sky's light gives (linear, the game's light units)
uniform float nightVisionHalfLuminance;  // night vision's calibration on the game's light scale (source/NightVision.h)
uniform float nightVisionSteepness;
uniform float nightVisionStrength;       // 0 none (colourful nights) .. 1 the eye
uniform vec3  nightVisionTint;           // the rods' grey, luminance 1 (the world's blue shift)

out vec4 finalColor;

#include "common/atmosphere"
#include "common/night_sky"
#include "common/night_vision"

// The moon's face where no texture is bound (sRGB grey).
const float MOON_FLAT_ALBEDO = 0.7;

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

// The moon's disc along a world ray: its radiance before the atmosphere's transmittance, and how much it covers what
// lies behind it (0 off the disc). The face is a sphere lit by the sun (sunDir), shaded with Lommel-Seeliger, the
// law of dusty surfaces like the moon's: normalized to 1 at full moon, where it makes the disc look flat as the real
// one does, and it puts the terminator and the crescent's orientation where the sun says. The dark side glows faintly
// with earthshine.
vec3 moonDisc(vec3 rayDir, vec3 sunDir, out float cover)
{
    cover = 0.0;
    if (dot(rayDir, moonDirection) < moonCosRadius)
    {
        return vec3(0.0);
    }
    vec2 disc = vec2(dot(rayDir, moonRight), dot(rayDir, moonUp))/moonSinRadius; // [-1, 1] across the disc
    float r2 = dot(disc, disc);
    if (r2 > 1.0)
    {
        return vec3(0.0);
    }
    float mu = sqrt(1.0 - r2);                                         // the surface's cosine toward us
    vec3 normal = disc.x*moonRight + disc.y*moonUp - mu*moonDirection; // the sphere's normal facing us
    float mu0 = dot(normal, sunDir);                                   // its cosine toward the sun
    float lit = (mu0 > 0.0) ? 2.0*mu0/(mu0 + mu) : 0.0;

    // The face: image right is the disc's right, image top its up (v = 0 is the top row as raylib loads it). Inside
    // this branch implicit derivatives are undefined, so the level is explicit (the texture has no mipmaps).
    vec4 texel = hasMoonTexture ? textureLod(moonTexture, vec2(0.5 + 0.5*disc.x, 0.5 - 0.5*disc.y), 0.0)
        : vec4(vec3(MOON_FLAT_ALBEDO), 1.0);
    vec3 albedo = pow(max(texel.rgb, vec3(0.0)), vec3(2.2)); // sRGB -> linear
    cover = clamp(texel.a, 0.0, 1.0)*moonOpacity;
    return albedo*max(moonColor, vec3(0.0))*moonDiscRadiance*(lit + moonEarthshine);
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
    vec3 transmittance;
    vec3 sky = atmosphere(skyRay, rayOrigin, sunDir, iSun, kMie, transmittance);
    if (moonSkyIntensity > 0.0)
    {
        // The moonlit sky: the same atmosphere lit by the moon (blue for the same reason the day is, far dimmer).
        sky += atmosphere(skyRay, rayOrigin, normalize(moonDirection), ATMOSPHERE_SUN_RADIANCE*moonSkyIntensity, kMie);
    }
    sky += nightSkySkyglow(skyRay.y, skyglowRadiance);
    float belowFactor = smoothstep(0.0, -0.12, rayDir.y); // 0 above the horizon, 1 below
    sky *= mix(1.0, 0.5, belowFactor);

    // Beyond the air, the moon covers what lies behind it: the sun disc, the Milky Way (and the stars, which the star
    // pass leaves out behind it). Only above the horizon, like the sun disc.
    float moonCover = 0.0;
    vec3 moon = vec3(0.0);
    if ((moonOpacity > 0.0) && (rayDir.y > -0.02))
    {
        moon = moonDisc(rayDir, sunDir, moonCover);
    }

    // The Milky Way, above the horizon only, dimmed near it like the stars. milkyWayRadiance already holds its
    // brightness, the light pollution and the twilight.
    if ((milkyWayRadiance > 0.0) && (rayDir.y > 0.0))
    {
        sky += milkyWay(rayDir)*nightSkyExtinction(nightSkyAirmass(rayDir.y))*(1.0 - moonCover);
    }

    // What the eye sees of the sky's dim light: at night its colour fades into the rods' grey (a low moon's sky is as
    // orange as a sunset's, but too dim for colour vision); by day this leaves it exactly as it is.
    vec3 tint = max(skyTint, vec3(0.0));
    sky = nightVision(sky*tint, nightVisionFieldLuminance, nightVisionHalfLuminance, nightVisionSteepness,
        nightVisionStrength, nightVisionTint);

    // The discs are emitters, far brighter than the game draws them, so they keep their colour: the sun disc, only
    // above the horizon (so the mirror does not create a second sun below), and the moon, dimmed and reddened by the air
    // in front of it (pale by day under the bright in-scatter, bright at night, orange as it rises).
    vec3 discs = vec3(0.0);
    float discCos = cos(radians(0.5*max(sunSize, 0.01)));
    if ((rayDir.y > -0.02) && (dot(rayDir, sunDir) > discCos))
    {
        discs += max(sunColor, vec3(0.0))*iSun*(1.0 - moonCover);
    }
    discs += moon*transmittance*moonCover;
    sky += discs*tint;

    finalColor = vec4(sky, 1.0); // linear HDR; tonemap post-pass maps to display
}
