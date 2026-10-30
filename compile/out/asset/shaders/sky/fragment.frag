#version 330

// Full-screen atmospheric sky: the atmosphere (shaders/common/atmosphere.glsl) lit by the sun and, at night, by the moon;
// light pollution's light; a sun disc, the moon's disc and the Milky Way. The eye's night vision
// (shaders/common/night_vision.glsl) greys the dim sky, not the discs. The stars are a separate pass drawn over this one
// (shaders/stars).
// The sky's glow comes from the atmosphere's tables, built first in the frame (the sky-view table by direction:
// single and multiple scattering, with light pollution's light; the transmittance table dims the discs and the Milky
// Way); with the tables off (the A/B, or unavailable) it is marched per pixel (single scattering, light pollution as
// common/night_sky's skyglow). The table holds the far ground below the horizon (the haze in front of it
// included), and the ground haze (common/haze) lies in front of the sky and the far ground as it lies in front of the
// world's surfaces, so a misty horizon meets misty ground without a seam.
// The weather's clouds (source/WorldWeatherLight.h), until clouds are drawn, are one even deck over the sky: above the
// horizon it shows the deck's radiance (an overcast sky's: brightest at the zenith, a third of it at the horizon) through
// the air in front of it, on top of the clear sky the clouds leave (the renderer dims the lights the tables are built
// with, and the discs, the stars and the Milky Way, by the share they leave), and a lightning flash's glow, most toward
// the strike.
// Drawn behind the 3D geometry into the linear-HDR scene buffer; the tonemap post-pass maps it to display.
// The per-pixel ray is reconstructed from the inverse view-projection so the sky lines up exactly with the 3D
// camera. The renderer works in render space (the world's orientation with the camera at the origin), so that matrix
// is a rotation and a projection: the point at clip depth 1 (the far plane's, or with reversed depth the near
// plane's) IS the ray, exact however far the camera is from the world's origin. Directions are the world's, with +Y up,
// matching WorldEnvironment_GetSunDirection. The C mirror of the ray is WorldRenderSpace_GetSkyRay
// (source/WorldRenderSpace.c).

in vec2 fragTexCoord;
in vec4 fragColor;

uniform vec2 resolution;       // scene target size in pixels
uniform mat4 invViewProj;      // inverse(view * proj) in render space (a rotation and the projection), matching BeginMode3D

uniform vec3 sunDirection;     // unit direction TO the sun (world, +Y up)
uniform vec3 sunIntensity;     // the sun above the air, colour in (linear): its light is ATMOSPHERE_SUN_LIGHT x this
uniform vec3 sunDiscIntensity; // the sun's disc, colour in (linear): its radiance is SUN_DISC_RADIANCE x this
uniform float sunSize;         // angular-size multiplier for the sun disc
uniform vec3 skyTint;          // linear multiplier, for alien skies

uniform vec3 skyglowRadiance;      // light pollution's glow at the horizon, linear (0 by day; the direct march's: the
                                   // tables hold light pollution's light in the air)
uniform sampler2D milkyWayTexture; // sky_milky_way: an equirectangular panorama in galactic coordinates (sRGB)
uniform mat4 worldToGalactic;      // world direction -> galactic (x toward the galactic centre, z its north pole)
uniform float milkyWayRadiance;    // linear radiance of a white texel; 0 draws no Milky Way (and samples nothing)
uniform float milkyWayLod;         // mip level matching the pixel's angular size (explicit: see milkyWay)

uniform vec3 moonDirection;        // unit direction TO the moon (world, +Y up)
uniform vec3 moonSkyIntensity;     // the moon's light on the atmosphere as sunIntensity is (colour in); 0 skips the
                                   // moonlit sky
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

uniform vec3 cloudZenithRadiance;  // the clouds' deck's radiance at the zenith, the share the clouds cover in (linear; 0:
                                   // no clouds)
uniform vec3 flashIrradiance;      // a lightning flash's light on a level surface (linear; 0: none)
uniform vec3 flashDirection;       // unit direction toward the flash's glow (up toward the clouds over the strike)
uniform float cloudHeight;         // the clouds' deck's height above the atmosphere's ground, metres

uniform float nightVisionFieldLuminance; // the scene luminance the sky's light gives (linear, the game's light units)
uniform float nightVisionHalfLuminance;  // night vision's calibration on the game's light scale (source/NightVision.h)
uniform float nightVisionSteepness;
uniform float nightVisionStrength;       // 0 none (colourful nights) .. 1 the eye
uniform vec3  nightVisionTint;           // the rods' grey, luminance 1 (the world's blue shift)

out vec4 finalColor;

#include "common/atmosphere"
#include "common/haze"
#include "common/night_sky"
#include "common/night_vision"

// The moon's face where no texture is bound (sRGB grey).
const float MOON_FLAT_ALBEDO = 0.7;
// The sun's disc's radiance at sun_intensity 1 (a constant of its own, not the sun's light ATMOSPHERE_SUN_LIGHT). Far
// dimmer than the real sun's, so that it keeps its colour on screen.
const float SUN_DISC_RADIANCE = 22.0;
// An overcast sky's zenith radiance per unit of its irradiance on a level surface (CIE: 9 / (7 pi)), and how its glow
// gathers toward the flash: a broad lobe of this power around the flash's direction, this many times the rest.
const float OVERCAST_ZENITH_PER_IRRADIANCE = 0.40926;
const float FLASH_LOBE_POWER = 6.0;
const float FLASH_LOBE_GAIN = 3.0;

// The air's transmittance from the camera to the clouds' deck along a ray above the horizon: its transmittance to the
// top over the transmittance to the top from where it meets the deck's height (1 with the camera at or above the deck).
// The distance to that height in the form that does not cancel for rays going up: (rd^2 - r0^2) / (r0 mu + sqrt(...)),
// with rd^2 - r0^2 carried as a difference of heights (common/atmosphere's rule).
vec3 weatherDeckTransmittance(vec3 rayDir)
{
    float h0 = atmosphereViewHeight;
    if (!(cloudHeight > h0))
    {
        return vec3(1.0);
    }
    float r0 = ATMOSPHERE_R_PLANET + h0;
    float rd = ATMOSPHERE_R_PLANET + cloudHeight;
    float mu = rayDir.y;
    float lift = (cloudHeight - h0)*(2.0*ATMOSPHERE_R_PLANET + cloudHeight + h0);
    float root = sqrt(max(r0*r0*mu*mu + lift, 0.0));
    float toDeck = (r0*mu > 0.0) ? lift/(r0*mu + root) : root - r0*mu;
    float muDeck = clamp((r0*mu + toDeck)/rd, -1.0, 1.0);
    return atmosphereTransmittanceToTop(h0, mu)/max(atmosphereTransmittanceToTop(cloudHeight, muDeck), vec3(1e-6));
}

// The weather's sky toward a direction above the horizon: the clouds' deck (an overcast sky's radiance, (1 + 2 cos
// zenith) / 3 of the zenith's) and a flash's glow, seen through the air's transmittance in front of them.
vec3 weatherSky(vec3 rayDir, vec3 transmittance)
{
    float gradient = (1.0 + 2.0*max(rayDir.y, 0.0))/3.0;
    vec3 deck = max(cloudZenithRadiance, vec3(0.0));
    if (any(greaterThan(flashIrradiance, vec3(0.0))))
    {
        float toward = max(dot(rayDir, normalize(flashDirection)), 0.0);
        float lobe = (1.0 + FLASH_LOBE_GAIN*pow(toward, FLASH_LOBE_POWER))/(1.0 + FLASH_LOBE_GAIN/(FLASH_LOBE_POWER + 1.0));
        deck += flashIrradiance*OVERCAST_ZENITH_PER_IRRADIANCE*lobe;
    }
    return deck*gradient*transmittance;
}

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
    vec3 rayDir = normalize(world.xyz/world.w); // the camera sits at the origin of render space
    vec3 sunDir = normalize(sunDirection);
    vec3 moonDir = normalize(moonDirection);
    vec3 iSun = ATMOSPHERE_SUN_LIGHT*max(sunIntensity, vec3(0.0));
    vec3 iMoon = ATMOSPHERE_SUN_LIGHT*max(moonSkyIntensity, vec3(0.0));
    bool isMoonlitSky = any(greaterThan(iMoon, vec3(0.0)));
    // With the tables the planet's surface ends every ray below its horizon (the far ground), and nothing beyond the
    // air shows there; the direct march mirrors the sky below the horizon and draws the discs down to -0.02.
    float toGround = atmosphereDistanceToGround(atmosphereViewHeight, rayDir.y);
    bool isBeyondAirShown = atmosphereTablesActive ? (toGround < 0.0) : (rayDir.y > -0.02);

    // The sky's glow, and the share of what lies beyond the air along the ray that reaches the camera (the discs, the
    // Milky Way). Below the horizon the tables hold the far ground; the direct march mirrors the sky above it and darkens
    // it, so it still reads as "down".
    vec3 tint = max(skyTint, vec3(0.0));
    vec3 sky;
    vec3 transmittance;
    vec3 milkyWayExtinction;
    if (atmosphereTablesActive)
    {
        // The tables: the sun's glow with light pollution's, and the moonlit sky (the same atmosphere lit by the
        // moon: blue for the same reason the day is, far dimmer), each with its multiple scattering, already tinted; the
        // far ground below the horizon is theirs.
        sky = atmosphereSkyView(rayDir, sunDir, 0.0);
        if (isMoonlitSky)
        {
            sky += atmosphereSkyView(rayDir, moonDir, 1.0);
        }
        transmittance = atmosphereTransmittanceToTop(atmosphereViewHeight, rayDir.y);
        // The Milky Way's brightness is tuned for the zenith: the air dims and reddens it by its transmittance relative
        // to the zenith's.
        milkyWayExtinction = transmittance/max(atmosphereTransmittanceToTop(atmosphereViewHeight, 1.0), vec3(1e-6));
    }
    else
    {
        // TODO: remove with the direct march once the owner approves the tables' sky.
        // The direct march, with the ray mirrored into the upper hemisphere (abs(y)) so every direction gets a sky
        // colour.
        vec3 skyRay = vec3(rayDir.x, abs(rayDir.y), rayDir.z);
        sky = atmosphere(skyRay, sunDir, iSun, transmittance);
        if (isMoonlitSky)
        {
            sky += atmosphere(skyRay, moonDir, iMoon);
        }
        sky += nightSkySkyglow(skyRay.y, skyglowRadiance);
        float belowFactor = smoothstep(0.0, -0.12, rayDir.y); // 0 above the horizon, 1 below
        sky *= mix(1.0, 0.5, belowFactor)*tint;
        milkyWayExtinction = vec3(nightSkyExtinction(nightSkyAirmass(rayDir.y)));
    }

    // The weather's clouds and a flash's glow over the sky above the horizon (the far ground below it is lit by them in
    // the table already).
    if (isBeyondAirShown && (any(greaterThan(cloudZenithRadiance, vec3(0.0)))
        || any(greaterThan(flashIrradiance, vec3(0.0)))))
    {
        sky += weatherSky(rayDir, atmosphereTablesActive ? weatherDeckTransmittance(rayDir) : transmittance)*tint;
    }

    // Beyond the air, the moon covers what lies behind it: the sun disc, the Milky Way (and the stars, which the star
    // pass leaves out behind it). Only above the horizon, like the sun disc.
    float moonCover = 0.0;
    vec3 moon = vec3(0.0);
    if ((moonOpacity > 0.0) && isBeyondAirShown)
    {
        moon = moonDisc(rayDir, sunDir, moonCover);
    }

    // The Milky Way, above the horizon only, dimmed near it like the stars. milkyWayRadiance already holds its
    // brightness, the light pollution and the twilight.
    if ((milkyWayRadiance > 0.0) && (rayDir.y > 0.0))
    {
        sky += milkyWay(rayDir)*milkyWayExtinction*(1.0 - moonCover)*tint;
    }

    // The ground haze in front of all of it: up to the far ground below the horizon (where the table's rays end),
    // through the whole sky above it, along the curved planet for the rays that pass over the horizon. It dims the discs
    // too.
    float mistTransmittance = 1.0;
    if (hazeActive && (groundHazeDensity > 0.0))
    {
        float toEnd = (toGround >= 0.0) ? toGround : ATMOSPHERE_SKY_DISTANCE;
        vec3 mistLight;
        mistTransmittance = hazeGroundInFrontOfSky(uv, rayDir, toEnd, (toGround < 0.0) && (rayDir.y < 0.0), sunDir,
            moonDir, mistLight);
        sky = mix(mistLight, sky, mistTransmittance);
    }

    // What the eye sees of the sky's dim light: at night its colour fades into the rods' grey (a low moon's sky is as
    // orange as a sunset's, but too dim for colour vision); by day this leaves it exactly as it is.
    sky = nightVision(sky, nightVisionFieldLuminance, nightVisionHalfLuminance, nightVisionSteepness,
        nightVisionStrength, nightVisionTint);

    // The discs are emitters, far brighter than the game draws them, so they keep their colour: the sun disc, only
    // above the horizon (not over the far ground, nor as a second sun in the direct march's mirror), and the moon, both
    // dimmed and reddened by the air in front of them (the moon pale by day under the bright in-scatter, bright at night,
    // orange as it rises; the sun orange as it sets). The direct march leaves the sun's disc undimmed.
    vec3 discs = vec3(0.0);
    float discCos = cos(radians(0.5*max(sunSize, 0.01)));
    if (isBeyondAirShown && (dot(rayDir, sunDir) > discCos))
    {
        vec3 sunDisc = SUN_DISC_RADIANCE*max(sunDiscIntensity, vec3(0.0))*(1.0 - moonCover);
        discs += atmosphereTablesActive ? (sunDisc*transmittance) : sunDisc;
    }
    discs += moon*transmittance*moonCover;
    sky += discs*tint*mistTransmittance;

    finalColor = vec4(sky, 1.0); // linear HDR; tonemap post-pass maps it to display
}
