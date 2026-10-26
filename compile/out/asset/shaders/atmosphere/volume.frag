#version 330

// The aerial-perspective volume (T35; Hillaire 2020, section 5.6): per texel of its texture (common/atmosphere's
// layout), for one of 32 x 32 screen directions at one of its slices, the light the air scattered toward the camera
// along the path so far (the in-scatter's tiles, in the sky's light units) or the path's transmittance (the
// transmittance's tiles); the last slice holds the whole ray, to the top of the atmosphere. Lit by the sun, by the moon
// while the moonlit sky shows, and by light pollution's light in the air. The last row holds the frame's values: the
// light at the ground haze's base, and (T36) the sky's light by height: the sun's and the moon's through the air, the
// sky's irradiance and the ground's bounce, what lights every surface at that height. Built every frame after the
// sky-view table, from the transmittance, multiple scattering and irradiance tables (source/WorldRendererAtmosphere.c;
// only its last row while the haze is off); the lit shaders, the sky pass and the outlines read it (common/haze,
// common/lighting).
// Its rays pass through the planet's surface, the air below it as on it: the world's ground is flat and goes on where
// the planet curves away (a level ray from 1.7 m meets the planet 4.65 km out), and a valley below sea level is hazed
// like the plain around it.

uniform mat4 invViewProjection;     // clip -> render space (a rotation and the projection): the screen's directions
uniform vec3 sunDirection;           // unit direction to the sun
uniform vec3 sunIntensity;           // the sun above the air, colour in: its light is ATMOSPHERE_SUN_LIGHT x this
uniform vec3 moonDirection;          // unit direction to the moon
uniform vec3 moonIntensity;          // the moon above the air the same way (the moonlit sky's light); 0: none
uniform float multipleScattering;    // the share of the multiple scattering drawn, 0..1
uniform vec3 lightPollutionRadiance; // the town's light in the air at the camera's foot (linear; 0 by day)
uniform vec3 skyTint;                // the sky's tint (linear): the haze is the sky's light, tinted as the sky is
uniform float groundHazeBaseHeight;  // the ground haze's base above the atmosphere's ground, metres
uniform vec3 groundAlbedo;           // the ground's albedo (sky_ground_albedo, linear): the ground's bounce

out vec4 finalColor;

#include "common/atmosphere"

// The largest value a texel of the volume (RGBA16F) holds.
const float HALF_FLOAT_MAX = 65504.0;

// One value of the sky's light (ATMOSPHERE_SKY_LIGHT_*) at one of its heights.
vec3 SkyLightValue(int value, float height, vec3 sunRadiance, vec3 moonRadiance)
{
    vec3 sun = sunRadiance*atmosphereLightTransmittance(height, sunDirection.y);
    vec3 moon = moonRadiance*atmosphereLightTransmittance(height, moonDirection.y);
    if (value == ATMOSPHERE_SKY_LIGHT_SUN)
    {
        return sun;
    }
    if (value == ATMOSPHERE_SKY_LIGHT_MOON)
    {
        return moon;
    }
    vec3 sky = (atmosphereSkyIrradiance(height, sunDirection.y)*sunRadiance
        + atmosphereSkyIrradiance(height, moonDirection.y)*moonRadiance
        + atmosphereLightPollutionIrradiance(height)*lightPollutionRadiance)*max(skyTint, vec3(0.0));
    if (value == ATMOSPHERE_SKY_LIGHT_SKY)
    {
        return sky;
    }
    // The ground below the surface, lit by the sun, the moon and the sky, reflecting evenly.
    return max(groundAlbedo, vec3(0.0))*(sun*max(sunDirection.y, 0.0) + moon*max(moonDirection.y, 0.0) + sky);
}

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    int size = int(ATMOSPHERE_VOLUME_SIZE);
    // A light far brighter than the sun saturates at the largest value a texel holds instead of turning infinite.
    vec3 sunRadiance = min(ATMOSPHERE_SUN_LIGHT*max(sunIntensity, vec3(0.0)), vec3(HALF_FLOAT_MAX));
    vec3 moonRadiance = min(ATMOSPHERE_SUN_LIGHT*max(moonIntensity, vec3(0.0)), vec3(HALF_FLOAT_MAX));
    if (texel.y >= 2*size)
    {
        // The values' row: the light at the ground haze's base, per unit of its scattering; then the sky's light by
        // height.
        float baseHeight = max(groundHazeBaseHeight, 0.0);
        vec3 value = vec3(0.0);
        int block = texel.x - ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN;
        if ((block >= 0) && (block < ATMOSPHERE_SKY_LIGHT_COUNT*ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT))
        {
            value = SkyLightValue(block/ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT,
                atmosphereSkyLightHeight(block % ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT), sunRadiance, moonRadiance);
        }
        else if (texel.x == ATMOSPHERE_VOLUME_GROUND_HAZE_SUN)
        {
            value = sunRadiance*atmosphereLightTransmittance(baseHeight, sunDirection.y);
        }
        else if (texel.x == ATMOSPHERE_VOLUME_GROUND_HAZE_MOON)
        {
            value = moonRadiance*atmosphereLightTransmittance(baseHeight, moonDirection.y);
        }
        else if (texel.x == ATMOSPHERE_VOLUME_GROUND_HAZE_AMBIENT)
        {
            // The sky's light from all around (its multiple scattering table, the light the air sends a point from
            // every direction), and the town's.
            value = sunRadiance*atmosphereMultipleScattering(baseHeight, sunDirection.y)
                + moonRadiance*atmosphereMultipleScattering(baseHeight, moonDirection.y)
                + lightPollutionRadiance*atmosphereLightPollutionShare(baseHeight, 0.0);
        }
        finalColor = vec4(min(value, vec3(HALF_FLOAT_MAX)), 1.0);
        return;
    }

    // Which tile (the in-scatter's or the transmittance's, and the slice) and which screen direction.
    bool isTransmittance = texel.y >= size;
    float slice = float(texel.x/size);
    vec2 screenUv = (vec2(texel.x % size, texel.y % size) + 0.5)/ATMOSPHERE_VOLUME_SIZE;
    vec4 world = invViewProjection*vec4(screenUv*2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(world.xyz/world.w); // the camera sits at the origin of render space

    // The path: the slice's distance, or the whole ray to the top (through the ground); no farther than the top.
    float toTop = atmosphereDistanceToTop(atmosphereViewHeight, direction.y);
    bool isWholeRay = slice > ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE;
    float sliceDistance = atmosphereVolumeSliceDistance(slice);
    float pathLength = isWholeRay ? toTop : min(sliceDistance, toTop);
    int stepCount = isWholeRay ? ATMOSPHERE_VOLUME_RAY_STEPS
        : ((sliceDistance <= ATMOSPHERE_VOLUME_NEAR_DISTANCE) ? ATMOSPHERE_VOLUME_NEAR_STEPS
            : ATMOSPHERE_VOLUME_FAR_STEPS);

    // The transmittance's tiles march the air alone; the in-scatter's light it by the sun with light pollution, and by
    // the moon.
    float cosSun = dot(direction, sunDirection);
    vec3 transmittance;
    vec3 light = atmosphereScatteredLight(atmosphereViewHeight, direction.y, sunDirection.y, cosSun, pathLength,
        stepCount, isTransmittance ? vec3(0.0) : sunRadiance, multipleScattering,
        isTransmittance ? vec3(0.0) : lightPollutionRadiance, transmittance);
    if (isTransmittance)
    {
        finalColor = vec4(transmittance, 1.0);
        return;
    }
    if (any(greaterThan(moonRadiance, vec3(0.0))))
    {
        vec3 moonTransmittance;
        light += atmosphereScatteredLight(atmosphereViewHeight, direction.y, moonDirection.y,
            dot(direction, moonDirection), pathLength, stepCount, moonRadiance, multipleScattering, vec3(0.0),
            moonTransmittance);
    }
    finalColor = vec4(min(light*max(skyTint, vec3(0.0)), vec3(HALF_FLOAT_MAX)), 1.0);
}
