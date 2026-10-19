#version 330

// The generated star catalogue, one star per one-pixel quad (WorldRendererStars.c). Every vertex of a star carries the
// star's direction, and its corner offset widens the star into a square exactly one scene pixel wide: such a square
// always covers exactly one pixel centre, so each star lights one pixel at a steady brightness (no flicker) and steps
// from pixel to pixel as the sky turns, like everything else in the pixelated world.

in vec3 vertexPosition;   // the star's unit direction in the star frame
in vec2 vertexTexCoord;   // corner offset in pixels (+-0.5, +-0.5)
in vec2 vertexTexCoord2;  // x = catalogue magnitude, y = twinkle seed in [0, 1)
in vec4 vertexColor;      // sRGB colour of the star's temperature (brightest channel 1)

uniform mat4 starsToWorld;       // turns the stars through the day and the year (WorldEnvironment_GetStarsToWorld)
uniform mat4 viewProjection;     // the frame's view x projection
uniform vec2 resolution;         // scene target size in pixels
uniform float limitingMagnitude; // faintest magnitude visible overhead (light pollution, twilight)
uniform float radianceScale;     // linear radiance of a magnitude-0 star, StarBrightness included
uniform float twinkleStrength;   // StarTwinkle, 0..1
uniform float timeSeconds;       // render time, for the twinkling
uniform vec3 moonDirection;      // the moon's direction: its disc hides the stars behind it
uniform float moonCosRadius;     // cosine of the disc's angular radius; above 1 while no disc is drawn

flat out vec3 fragRadiance;

#include "common/night_sky"

// Stars fade out within this many magnitudes of the limit, so at dusk the brightest appear first.
const float STAR_FADE_MAGNITUDES = 0.5;
// Twinkle speed in noise cells per second, varied per star by 0.75..1.25.
const float STAR_TWINKLE_RATE = 4.0;
// Twinkling grows with the air mass (scintillation is strongest near the horizon), up to this air mass.
const float STAR_TWINKLE_MAX_AIRMASS = 4.0;

float hash11(float p)
{
    p = fract(p*0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

// Smooth 1D value noise in [0, 1].
float noise11(float x)
{
    float cell = floor(x);
    float t = fract(x);
    t = t*t*(3.0 - 2.0*t);
    return mix(hash11(cell), hash11(cell + 1.0), t);
}

void main()
{
    vec3 direction = normalize(mat3(starsToWorld)*vertexPosition);
    float airmass = nightSkyAirmass(direction.y);
    float magnitude = vertexTexCoord2.x + NIGHT_SKY_EXTINCTION_PER_AIRMASS*(airmass - 1.0);
    float visibility = clamp((limitingMagnitude - magnitude)/STAR_FADE_MAGNITUDES, 0.0, 1.0);
    if ((direction.y <= 0.0) || (visibility <= 0.0) || (dot(direction, moonDirection) > moonCosRadius))
    {
        // Below the horizon, too faint or behind the moon: the quad goes outside the clip volume, so nothing is
        // rasterized.
        fragRadiance = vec3(0.0);
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    float seed = vertexTexCoord2.y;
    float twinkle = noise11(timeSeconds*STAR_TWINKLE_RATE*(0.75 + 0.5*seed) + seed*1000.0) - 0.5;
    float twinkleScale = 0.5*min(airmass, STAR_TWINKLE_MAX_AIRMASS);
    float flux = exp2(-1.3287712*magnitude)*max(1.0 + twinkleStrength*twinkle*twinkleScale, 0.0); // 10^(-0.4 m)

    // One luminance per magnitude whatever the colour: the colour only sets the hue.
    vec3 color = pow(max(vertexColor.rgb, vec3(0.0)), vec3(2.2));
    color /= max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
    fragRadiance = color*flux*radianceScale*visibility;

    // A point at infinity (w = 0, so the camera's position does not matter), just inside the far plane, widened by the
    // corner offset to exactly one pixel.
    vec4 clip = viewProjection*vec4(direction, 0.0);
    clip.z = clip.w*0.9999;
    clip.xy += vertexTexCoord*2.0/resolution*clip.w;
    gl_Position = clip;
}
