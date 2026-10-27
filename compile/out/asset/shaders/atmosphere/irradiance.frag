#version 330

// The sky's irradiance table (T36, one light scale; common/atmosphere's layout): per texel, the light the sky sends a
// surface facing up at a height, per unit of a light above the air at an elevation (the sun's or the moon's), from the
// same model as the sky: single scattering through the transmittance table with the phase functions, plus
// multipleScattering of the multiple scattering table's glow; the last column light pollution's light in the air (per unit
// of its radiance at the camera's foot) by height. Stored as logarithms. Drawn whenever the air changes (after the
// transmittance and multiple scattering tables, which it reads) or the haze's forward scattering or the share of multiple
// scattering does (source/WorldRendererAtmosphere.c). The volume's builder and the sky view's read it.
//
// THE QUADRATURE is the C mirror's (source/WorldSkyLight.c, WorldSkyLight_GetSkyIrradiance: keep the two in step;
// tests/ShaderSourceTests.c pins these constants to its macros):
//  - the air's own light (Rayleigh, the multiple scattering): Gauss-Legendre rings of the zenith cosine over the upper
//    half, even azimuths over half a turn (the sky is symmetric about the light's vertical plane); the multiple
//    scattering's glow once a ring, at the mean of its azimuths' light angles;
//  - light pollution's, which fills the low air near the camera evenly: the same rings without azimuths, each up to
//    IRRADIANCE_LIGHT_POLLUTION_DEPTH of its share's falloff lengths along it;
//  - the haze's single scattering, a narrow lobe around the light: directions around it by Henyey and Greenstein's
//    inverse with the haze's forward scattering, weighted by Cornette and Shanks's over it, the upper half only, each up
//    to IRRADIANCE_MIE_DEPTH of the haze's scale heights above the surface;
//  - Simpson's rule along each direction over stretches growing with the square of their index, the optical depth between
//    the points exact for the air's exponential densities.

uniform float multipleScattering;  // the share of the multiple scattering drawn, 0..1

out vec4 finalColor;

#include "common/atmosphere"

const int IRRADIANCE_ZENITH_NODES = 6;
const int IRRADIANCE_AZIMUTHS = 6;
const int IRRADIANCE_MIE_RINGS = 6;
const int IRRADIANCE_MIE_AZIMUTHS = 6;
const int IRRADIANCE_STEPS = 8;
const int IRRADIANCE_MIE_STEPS = 4;
const float IRRADIANCE_MIE_DEPTH = 10.0;
const float IRRADIANCE_LIGHT_POLLUTION_DEPTH = 10.0;
const int IRRADIANCE_MAX_POINTS = 2*IRRADIANCE_STEPS + 1;
// Below this ((1 - e^-x) / x's argument) the series (the C mirror's SKY_LIGHT_SHARE_SERIES_LIMIT).
const float IRRADIANCE_SERIES_LIMIT = 1e-3;
// Below this the haze's forward scattering counts as none (the C mirror's SKY_LIGHT_ISOTROPIC_LIMIT).
const float IRRADIANCE_ISOTROPIC_LIMIT = 1e-4;
// The largest forward scattering (WORLD_ENVIRONMENT_MAX_SUN_GLOW).
const float IRRADIANCE_MAX_GLOW = 0.99;

// Gauss-Legendre on [-1, 1], IRRADIANCE_ZENITH_NODES nodes.
const float GAUSS_NODES[IRRADIANCE_ZENITH_NODES] = float[](-0.9324695142031521, -0.6612093864662645,
    -0.2386191860831969, 0.2386191860831969, 0.6612093864662645, 0.9324695142031521);
const float GAUSS_WEIGHTS[IRRADIANCE_ZENITH_NODES] = float[](0.1713244923791704, 0.3607615730481386,
    0.4679139345726910, 0.4679139345726910, 0.3607615730481386, 0.1713244923791704);

// One direction's points: their distances, heights, the air's scattering there (Rayleigh's, the haze's) and Simpson's
// weight times the view's transmittance to them.
float pointDistance[IRRADIANCE_MAX_POINTS];
float pointHeight[IRRADIANCE_MAX_POINTS];
vec3 pointRayleigh[IRRADIANCE_MAX_POINTS];
vec3 pointMie[IRRADIANCE_MAX_POINTS];
vec3 pointWeight[IRRADIANCE_MAX_POINTS];

// The mean of an exponential density between two points from its values there (heights at least 0), exact for a height
// changing linearly between them.
float MeanDensity(float densityA, float densityB, float heightA, float heightB, float scale)
{
    float low = max(densityA, densityB);
    float high = min(densityA, densityB);
    float x = abs(heightA - heightB)/scale;
    return (x > IRRADIANCE_SERIES_LIMIT) ? (low - high)/x : low*(1.0 - 0.5*x);
}

float StretchStart(float pathLength, int stretch, int stretches)
{
    float unit = float(stretch)/float(stretches);
    return pathLength*unit*unit;
}

// The points of one direction from a surface `height` up climbing with `cosZenith`, `pathLength` long, in `stretches`
// stretches (2 x stretches + 1 points).
void BuildRay(float height, float cosZenith, float pathLength, int stretches)
{
    float mieHeight = max(atmosphereMieHeight, 1.0);
    vec3 transmittance = vec3(1.0);
    float previousDistance = 0.0;
    float previousHeight = 0.0;
    vec3 previousDensities = vec3(0.0);
    for (int index = 0; index < 2*stretches + 1; index++)
    {
        int stretch = index/2;
        float from = StretchStart(pathLength, stretch, stretches);
        float to = StretchStart(pathLength, stretch + 1, stretches);
        bool isMiddle = (index % 2) == 1;
        float distance = isMiddle ? 0.5*(from + to) : from;
        float before = (stretch > 0) ? (from - StretchStart(pathLength, stretch - 1, stretches))/6.0 : 0.0;
        float after = (stretch < stretches) ? (to - from)/6.0 : 0.0;
        float weight = isMiddle ? 4.0*(to - from)/6.0 : before + after;
        float h = max((index == 0) ? height : atmosphereHeightAt(height, cosZenith, distance), 0.0);
        vec3 densities = vec3(exp(-h/ATMOSPHERE_RAYLEIGH_HEIGHT), exp(-h/mieHeight), atmosphereOzoneDensity(h));
        if (index > 0)
        {
            float rayleigh = MeanDensity(previousDensities.x, densities.x, previousHeight, h, ATMOSPHERE_RAYLEIGH_HEIGHT);
            float mie = MeanDensity(previousDensities.y, densities.y, previousHeight, h, mieHeight);
            float ozone = 0.5*(previousDensities.z + densities.z);
            vec3 depth = (atmosphereRayleighScattering*rayleigh + atmosphereMieExtinction*mie
                + atmosphereOzoneAbsorption*ozone)*(distance - previousDistance);
            transmittance *= exp(-depth);
        }
        pointDistance[index] = distance;
        pointHeight[index] = h;
        pointRayleigh[index] = atmosphereRayleighScattering*densities.x;
        pointMie[index] = atmosphereMieScattering*densities.y;
        pointWeight[index] = weight*transmittance;
        previousDistance = distance;
        previousHeight = h;
        previousDensities = densities;
    }
}

// The single scattering along the built direction from a light at `lightCosZenith` at the surface (`radius` from the
// planet's centre), `cosLightAngle` between them, with the phases given.
vec3 RayLight(int pointCount, float radius, float lightCosZenith, float cosLightAngle, float rayleighPhase,
    float miePhase)
{
    vec3 light = vec3(0.0);
    for (int index = 0; index < pointCount; index++)
    {
        float lightCos = clamp((radius*lightCosZenith + pointDistance[index]*cosLightAngle)
            /(ATMOSPHERE_R_PLANET + pointHeight[index]), -1.0, 1.0);
        light += pointWeight[index]*(pointRayleigh[index]*rayleighPhase + pointMie[index]*miePhase)
            *atmosphereLightTransmittance(pointHeight[index], lightCos);
    }
    return light;
}

// The multiple scattering's glow along the built direction, at the mean light angle of its ring's azimuths.
vec3 RayGlow(int pointCount, float radius, float cosZenith, float lightCosZenith)
{
    vec3 glow = vec3(0.0);
    float meanCosLightAngle = cosZenith*lightCosZenith;
    for (int index = 0; index < pointCount; index++)
    {
        float lightCos = clamp((radius*lightCosZenith + pointDistance[index]*meanCosLightAngle)
            /(ATMOSPHERE_R_PLANET + pointHeight[index]), -1.0, 1.0);
        glow += pointWeight[index]*(pointRayleigh[index] + pointMie[index])
            *atmosphereMultipleScattering(pointHeight[index], lightCos);
    }
    return glow;
}

float HenyeyGreensteinPhase(float cosAngle, float g)
{
    float gg = g*g;
    return (1.0 - gg)/(4.0*PI*pow(1.0 + gg - 2.0*g*cosAngle, 1.5));
}

float HenyeyGreensteinCos(float quantile, float g)
{
    if (abs(g) < IRRADIANCE_ISOTROPIC_LIMIT)
    {
        return 1.0 - 2.0*quantile;
    }
    float q = (1.0 - g*g)/(1.0 - g + 2.0*g*quantile);
    return clamp((1.0 + g*g - q*q)/(2.0*g), -1.0, 1.0);
}

// The distance along a climbing ray to `rise` metres above its start.
float DistanceToRise(float height, float cosZenith, float rise)
{
    float radius = ATMOSPHERE_R_PLANET + height;
    return -radius*cosZenith + sqrt(radius*radius*cosZenith*cosZenith + rise*(2.0*radius + rise));
}

// The sky's irradiance per unit of a light above the air at `lightCosZenith`, on a surface `height` up.
vec3 SkyIrradiance(float height, float lightCosZenith)
{
    float lightSin = sqrt(max(1.0 - lightCosZenith*lightCosZenith, 0.0));
    float radius = ATMOSPHERE_R_PLANET + height;
    float share = clamp(multipleScattering, 0.0, 1.0);
    vec3 irradiance = vec3(0.0);
    int pointCount = 2*IRRADIANCE_STEPS + 1;
    for (int ring = 0; ring < IRRADIANCE_ZENITH_NODES; ring++)
    {
        float cosZenith = 0.5*(GAUSS_NODES[ring] + 1.0);
        float sinZenith = sqrt(max(1.0 - cosZenith*cosZenith, 0.0));
        BuildRay(height, cosZenith, atmosphereDistanceToTop(height, cosZenith), IRRADIANCE_STEPS);
        float ringWeight = 0.5*GAUSS_WEIGHTS[ring]*cosZenith*2.0*PI;
        for (int azimuth = 0; azimuth < IRRADIANCE_AZIMUTHS; azimuth++)
        {
            float angle = PI*(float(azimuth) + 0.5)/float(IRRADIANCE_AZIMUTHS);
            float cosLightAngle = sinZenith*lightSin*cos(angle) + cosZenith*lightCosZenith;
            irradiance += ringWeight/float(IRRADIANCE_AZIMUTHS)
                *RayLight(pointCount, radius, lightCosZenith, cosLightAngle, atmosphereRayleighPhase(cosLightAngle), 0.0);
        }
        if (share > 0.0)
        {
            irradiance += ringWeight*share*RayGlow(pointCount, radius, cosZenith, lightCosZenith);
        }
    }
    // The haze's lobe around the light.
    float g = clamp(atmosphereMieAnisotropy, 0.0, IRRADIANCE_MAX_GLOW);
    float rise = IRRADIANCE_MIE_DEPTH*max(atmosphereMieHeight, 1.0);
    float count = float(IRRADIANCE_MIE_RINGS*IRRADIANCE_MIE_AZIMUTHS);
    int miePointCount = 2*IRRADIANCE_MIE_STEPS + 1;
    for (int ring = 0; ring < IRRADIANCE_MIE_RINGS; ring++)
    {
        float cosAngle = HenyeyGreensteinCos((float(ring) + 0.5)/float(IRRADIANCE_MIE_RINGS), g);
        float sinAngle = sqrt(max(1.0 - cosAngle*cosAngle, 0.0));
        float phaseWeight = atmosphereCornetteShanksPhase(cosAngle, g)/HenyeyGreensteinPhase(cosAngle, g);
        for (int azimuth = 0; azimuth < IRRADIANCE_MIE_AZIMUTHS; azimuth++)
        {
            float angle = PI*(float(azimuth) + 0.5)/float(IRRADIANCE_MIE_AZIMUTHS);
            float cosZenith = cosAngle*lightCosZenith + sinAngle*cos(angle)*lightSin;
            if (!(cosZenith > 0.0))
            {
                continue;
            }
            float pathLength = min(atmosphereDistanceToTop(height, cosZenith), DistanceToRise(height, cosZenith, rise));
            BuildRay(height, cosZenith, pathLength, IRRADIANCE_MIE_STEPS);
            irradiance += phaseWeight*cosZenith/count*RayLight(miePointCount, radius, lightCosZenith, cosAngle, 0.0, 1.0);
        }
    }
    return irradiance;
}

// Light pollution's irradiance per unit of its radiance at the camera's foot, on a surface `height` up above it.
vec3 LightPollutionIrradiance(float height)
{
    vec3 irradiance = vec3(0.0);
    int pointCount = 2*IRRADIANCE_STEPS + 1;
    for (int ring = 0; ring < IRRADIANCE_ZENITH_NODES; ring++)
    {
        float cosZenith = 0.5*(GAUSS_NODES[ring] + 1.0);
        float sinZenith = sqrt(max(1.0 - cosZenith*cosZenith, 0.0));
        // Up to its share's falloff lengths along the ray (its height's and its reach's): the rest holds none of it.
        float falloff = 1.0/(cosZenith/ATMOSPHERE_LIGHT_POLLUTION_HEIGHT + sinZenith/ATMOSPHERE_LIGHT_POLLUTION_REACH);
        BuildRay(height, cosZenith, min(atmosphereDistanceToTop(height, cosZenith),
            IRRADIANCE_LIGHT_POLLUTION_DEPTH*falloff), IRRADIANCE_STEPS);
        float ringWeight = 0.5*GAUSS_WEIGHTS[ring]*cosZenith*2.0*PI;
        for (int index = 0; index < pointCount; index++)
        {
            irradiance += ringWeight*pointWeight[index]*(pointRayleigh[index] + pointMie[index])
                *atmosphereLightPollutionShare(pointHeight[index], pointDistance[index]*sinZenith);
        }
    }
    return irradiance;
}

void main()
{
    int column = int(gl_FragCoord.x);
    float height = atmosphereIrradianceRowHeight(floor(gl_FragCoord.y)/(ATMOSPHERE_IRRADIANCE_HEIGHT - 1.0));
    vec3 irradiance;
    if (column >= int(ATMOSPHERE_IRRADIANCE_WIDTH))
    {
        irradiance = LightPollutionIrradiance(height);
    }
    else
    {
        float elevation = atmosphereIrradianceColumnElevation(float(column)/(ATMOSPHERE_IRRADIANCE_WIDTH - 1.0));
        irradiance = SkyIrradiance(height, sin(elevation));
    }
    // A boolean select: no light (or a NaN from absurd air) gives the floor, never log(0) or a NaN.
    finalColor = vec4(mix(vec3(ATMOSPHERE_IRRADIANCE_LOG_FLOOR), log(irradiance),
        greaterThan(irradiance, vec3(exp(ATMOSPHERE_IRRADIANCE_LOG_FLOOR)))), 1.0);
}
