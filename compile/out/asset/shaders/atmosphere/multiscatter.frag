#version 330

// The atmosphere's multiple scattering table (Hillaire 2020, section 5.5): per texel, the light scattered twice or
// more toward a point, by its height and the light's zenith angle, for a light of radiance 1, per unit of the air's
// scattering (the sky-view pass multiplies it by the scattering at each step, as an even glow). From 64 directions around
// the point: the light the air scatters toward it once more (the second order, with an even phase function, and the
// light the ground reflects), and the share of light the air around it sends back (f_ms); every higher order is that
// share again, a geometric series: L2 / (1 - f_ms). Drawn whenever the air changes, after the transmittance table, which
// it reads (source/WorldRendererAtmosphere.c).

uniform vec3 groundAlbedo; // the ground's albedo (linear): the light it reflects up into the air

out vec4 finalColor;

#include "common/atmosphere"

// The directions around each point: a stratified grid on the sphere, this many by this many (64 in all).
const int MULTISCATTER_SQRT_DIRECTIONS = 8;
// Steps along each direction, growing with the square of their index.
const int MULTISCATTER_STEPS = 20;

// One direction's march from a point `height` metres up: the light scattered toward the point (`secondOrder`, with an
// even phase function, and the lit ground's if the direction meets it) and the air's scattering seen along it
// (`transfer`), for a light whose zenith cosine at the point is `lightCosZenith`, `cosLightAngle` the cosine between the
// direction and the light.
void MarchDirection(float height, float cosZenith, float lightCosZenith, float cosLightAngle, out vec3 secondOrder,
    out vec3 transfer)
{
    float radius = ATMOSPHERE_R_PLANET + height;
    float pathLength = atmosphereDistanceToTop(height, cosZenith);
    float toGround = atmosphereDistanceToGround(height, cosZenith);
    bool isGroundHit = toGround >= 0.0;
    if (isGroundHit)
    {
        pathLength = min(pathLength, toGround);
    }
    float mieHeight = max(atmosphereMieHeight, 1.0);
    float steps = float(MULTISCATTER_STEPS);
    secondOrder = vec3(0.0);
    transfer = vec3(0.0);
    vec3 depth = vec3(0.0);
    for (int i = 0; i < MULTISCATTER_STEPS; i++)
    {
        float from = float(i)/steps;
        float to = float(i + 1)/steps;
        float stepLength = pathLength*(to*to - from*from);
        float t = pathLength*from*from + 0.5*stepLength;
        float h = atmosphereHeightAt(height, cosZenith, t);
        float lightCos = clamp((radius*lightCosZenith + t*cosLightAngle)/(ATMOSPHERE_R_PLANET + h), -1.0, 1.0);
        float densityM = exp(-h/mieHeight);
        vec3 scattering = atmosphereRayleighScattering*exp(-h/ATMOSPHERE_RAYLEIGH_HEIGHT)
            + atmosphereMieScattering*densityM;
        vec3 extinction = atmosphereRayleighScattering*exp(-h/ATMOSPHERE_RAYLEIGH_HEIGHT)
            + atmosphereMieExtinction*densityM + atmosphereOzoneAbsorption*atmosphereOzoneDensity(h);
        vec3 stepDepth = extinction*stepLength;
        vec3 throughput = exp(-depth)*stepLength*atmosphereStepShare(stepDepth);
        secondOrder += throughput*scattering*atmosphereLightTransmittance(h, lightCos)/(4.0*PI);
        transfer += throughput*scattering;
        depth += stepDepth;
    }
    if (isGroundHit)
    {
        // The ground reflects the light that reaches it, evenly (a Lambertian surface of groundAlbedo).
        float groundLightCos = clamp((radius*lightCosZenith + pathLength*cosLightAngle)/ATMOSPHERE_R_PLANET, -1.0, 1.0);
        secondOrder += exp(-depth)*atmosphereLightTransmittance(0.0, groundLightCos)*max(groundLightCos, 0.0)
            *max(groundAlbedo, vec3(0.0))/PI;
    }
}

void main()
{
    vec2 uv = gl_FragCoord.xy/vec2(ATMOSPHERE_MULTISCATTER_SIZE);
    float height;
    float lightCosZenith;
    atmosphereMultiscatterRay(uv, height, lightCosZenith);
    float lightSinZenith = sqrt(max(1.0 - lightCosZenith*lightCosZenith, 0.0));

    // The mean over the sphere of each direction's light and transfer (the integral over the sphere with an even phase
    // function, 1 / 4 pi).
    vec3 secondOrder = vec3(0.0);
    vec3 transfer = vec3(0.0);
    for (int a = 0; a < MULTISCATTER_SQRT_DIRECTIONS; a++)
    {
        for (int b = 0; b < MULTISCATTER_SQRT_DIRECTIONS; b++)
        {
            float azimuth = 2.0*PI*(float(a) + 0.5)/float(MULTISCATTER_SQRT_DIRECTIONS);
            float cosZenith = 1.0 - 2.0*(float(b) + 0.5)/float(MULTISCATTER_SQRT_DIRECTIONS);
            float sinZenith = sqrt(max(1.0 - cosZenith*cosZenith, 0.0));
            // The light lies in the plane of azimuth 0.
            float cosLightAngle = cos(azimuth)*sinZenith*lightSinZenith + cosZenith*lightCosZenith;
            vec3 directionLight;
            vec3 directionTransfer;
            MarchDirection(height, cosZenith, lightCosZenith, cosLightAngle, directionLight, directionTransfer);
            secondOrder += directionLight;
            transfer += directionTransfer;
        }
    }
    float directionCount = float(MULTISCATTER_SQRT_DIRECTIONS*MULTISCATTER_SQRT_DIRECTIONS);
    secondOrder /= directionCount;
    transfer /= directionCount;
    finalColor = vec4(secondOrder/max(vec3(1.0) - transfer, vec3(1e-3)), 1.0);
}
