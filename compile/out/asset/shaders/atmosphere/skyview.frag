#version 330

// The atmosphere's sky-view table (T15): per texel of one half, the light the sky sends toward the camera from a
// direction, seen from the camera's height (common/atmosphere's layout), lit by one light: the sun in the first half
// (with light pollution's skyglow), the moon in the second (while the moonlit sky shows). Single scattering through the
// transmittance table, plus the multiple scattering table's glow. Drawn every frame, one half at a time, before
// everything that reads it (source/WorldRendererAtmosphere.c): the sky pass and the fog sample it by direction.

uniform float lightCosZenith;      // the light's zenith cosine (its direction's y)
uniform float lightIntensity;      // its light on the air relative to the sun's at sunIntensity 1 (the sun's raw
                                   // intensity, or the moonlit sky's)
uniform float multipleScattering;  // the share of the multiple scattering drawn, 0..1
uniform float tableHalf;           // the half drawn: 0 the sun's, 1 the moon's
uniform vec3 skyglowRadiance;      // light pollution's glow at the horizon (linear; 0 by day, and in the moon's half)

out vec4 finalColor;

#include "common/atmosphere"
#include "common/night_sky"

// Steps along each view ray, growing with the square of their index (T15's status block has their accuracy).
const int SKY_VIEW_STEPS = 32;
// The largest value a texel of the table (RGBA16F) holds.
const float HALF_FLOAT_MAX = 65504.0;

void main()
{
    // Each half is drawn over the whole texture: the other half's rows are left as they are.
    if (floor(gl_FragCoord.y/ATMOSPHERE_SKY_VIEW_HEIGHT) != tableHalf)
    {
        discard;
    }
    vec2 unit = vec2(atmosphereTexelToUnit(gl_FragCoord.x/ATMOSPHERE_SKY_VIEW_WIDTH, ATMOSPHERE_SKY_VIEW_WIDTH),
        atmosphereTexelToUnit(gl_FragCoord.y/ATMOSPHERE_SKY_VIEW_HEIGHT - tableHalf, ATMOSPHERE_SKY_VIEW_HEIGHT));
    float cosZenith;
    float cosLightAzimuth;
    atmosphereSkyViewDirection(atmosphereViewHeight, unit, cosZenith, cosLightAzimuth);

    // Below the horizon, for now, the sky's darkened mirror: the sky above it, darker the lower (T35 draws the far ground
    // there instead).
    // TODO: T35 replaces the darkened mirror (this line and the darkening below) with the far ground, and the skyglow
    // line with the physical skyglow (the town's light scattered by the air).
    float skyCos = abs(cosZenith);
    float skySin = sqrt(max(1.0 - skyCos*skyCos, 0.0));
    float lightSin = sqrt(max(1.0 - lightCosZenith*lightCosZenith, 0.0));
    float cosLightAngle = skySin*lightSin*cosLightAzimuth + skyCos*lightCosZenith;
    vec3 transmittance;
    // A light far brighter than the sun saturates at the largest value the table's half floats hold, instead of turning
    // infinite there (and NaN wherever it meets a 0).
    float lightRadiance = min(ATMOSPHERE_SUN_RADIANCE*max(lightIntensity, 0.0), HALF_FLOAT_MAX);
    vec3 sky = lightRadiance*atmosphereScatteredLight(atmosphereViewHeight, skyCos,
        lightCosZenith, cosLightAngle, 1e30, SKY_VIEW_STEPS, multipleScattering, transmittance);
    sky += nightSkySkyglow(skyCos, skyglowRadiance);
    sky *= mix(1.0, 0.5, 1.0 - smoothstep(-0.12, 0.0, cosZenith));
    finalColor = vec4(min(sky, vec3(HALF_FLOAT_MAX)), 1.0);
}
