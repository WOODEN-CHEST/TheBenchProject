#version 330

// The atmosphere's sky-view table: per texel of one half, the light the sky sends toward the camera from a
// direction, seen from the camera's height (common/atmosphere's layout), lit by one light: the sun in the first half
// (with light pollution's light in the air), the moon in the second (while the moonlit sky shows). Single scattering
// through the transmittance table, plus the multiple scattering table's glow. Drawn every frame, one half at a time,
// before everything that reads it (source/WorldRendererAtmosphere.c): the sky pass samples it by direction.
//
// Below the horizon a ray ends on the far ground: the planet's surface at the atmosphere's ground height, seen
// through the haze in front of it. The ground is the frame's sky_ground_albedo lit as the lit shaders light a dielectric
// facing up, so the far ground meets a terrain's edge without a seam: by the half's light (the sun's, or the
// moon's) through the air at that point of the curved planet (reddened as it sets there, none once it has set) and the
// sky's irradiance there (the irradiance table at the light's own height over the point; in the sun's half light
// pollution's and the night's own light too), tinted as the lit shaders tint the ambient. The haze in front of it
// follows the frame's haze (the distance scale and the start), as the volume's does for everything else, its rays through the ground
// (common/atmosphere) where the scale takes them past it. The sky's in-scatter is tinted by the sky's tint; the ground's
// direct light is not.

uniform float lightCosZenith;      // the light's zenith cosine (its direction's y)
uniform vec3 lightIntensity;       // its light above the air, colour in: ATMOSPHERE_SUN_LIGHT x this (the sun's, or the
                                   // moonlit sky's)
uniform float multipleScattering;  // the share of the multiple scattering drawn, 0..1
uniform float tableHalf;           // the half drawn: 0 the sun's, 1 the moon's
uniform vec3 lightPollutionRadiance; // the town's light in the air at the camera's foot (linear; 0 by day, and in the
                                     // moon's half)
uniform vec3 skyTint;              // the sky's tint (linear)
uniform float hazeDistanceScale;   // the frame's haze: every distance x this, none nearer than hazeStartDistance
uniform float hazeStartDistance;
uniform vec3 groundAlbedo;         // the far ground's albedo (sky_ground_albedo, linear)
uniform vec3 ambientTint;          // the ambient's tint on the sky's light (ambient_skylight_color x intensity)
uniform vec3 groundNightLight;     // the moonless night's own light on the far ground per unit of albedo (0 in the
                                   // moon's half)

out vec4 finalColor;

#include "common/atmosphere"

// Steps along each view ray, growing with the square of their index.
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
    float sinZenith = sqrt(max(1.0 - cosZenith*cosZenith, 0.0));
    float lightSin = sqrt(max(1.0 - lightCosZenith*lightCosZenith, 0.0));
    float cosLightAngle = sinZenith*lightSin*cosLightAzimuth + cosZenith*lightCosZenith;
    // A light far brighter than the sun saturates at the largest value the table's half floats hold, instead of turning
    // infinite there (and NaN wherever it meets a 0).
    vec3 lightRadiance = min(ATMOSPHERE_SUN_LIGHT*max(lightIntensity, vec3(0.0)), vec3(HALF_FLOAT_MAX));

    // Above the horizon the ray crosses the whole atmosphere; below it, the haze in front of the far ground.
    float toGround = atmosphereDistanceToGround(atmosphereViewHeight, cosZenith);
    bool isGround = toGround >= 0.0;
    float pathLength = isGround
        ? max(toGround - hazeStartDistance, 0.0)*max(hazeDistanceScale, 0.0)
        : atmosphereDistanceToTop(atmosphereViewHeight, cosZenith);
    vec3 transmittance;
    vec3 sky = atmosphereScatteredLight(atmosphereViewHeight, cosZenith, lightCosZenith, cosLightAngle, pathLength,
        SKY_VIEW_STEPS, lightRadiance, multipleScattering, lightPollutionRadiance, transmittance)*max(skyTint, vec3(0.0));
    if (isGround)
    {
        // The light's own zenith cosine over the ground point (WorldAtmosphere_GetLightCosAt), where it is lit like the
        // lit shaders' ground: through the air there, by the cosine to the light, and the sky's irradiance there.
        float radius = ATMOSPHERE_R_PLANET + atmosphereViewHeight;
        float groundCos = clamp((radius*lightCosZenith + toGround*cosLightAngle)/ATMOSPHERE_R_PLANET, -1.0, 1.0);
        vec3 direct = lightRadiance*atmosphereLightTransmittance(0.0, groundCos)*max(groundCos, 0.0);
        vec3 skyLight = (atmosphereSkyIrradiance(0.0, groundCos)*lightRadiance
            + atmosphereLightPollutionIrradiance(0.0)*lightPollutionRadiance)*max(skyTint, vec3(0.0));
        vec3 ground = groundAlbedo*((direct + skyLight*max(ambientTint, vec3(0.0)))/PI
            + max(groundNightLight, vec3(0.0)));
        sky += transmittance*max(ground, vec3(0.0));
    }
    finalColor = vec4(min(sky, vec3(HALF_FLOAT_MAX)), 1.0);
}
