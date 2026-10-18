// Rayleigh + Mie single-scattering atmosphere, for #include "common/atmosphere". The sky pass (shaders/sky) draws it,
// and the PBR shader's distance fog (shaders/pbr) fades distant geometry toward it, so a fogged silhouette meets
// exactly the sky behind it. World space, +Y up, distances in metres.

#include "common/math"

// Atmosphere constants (metres).
const int   ATMOSPHERE_PRIMARY_STEPS = 16;
const int   ATMOSPHERE_LIGHT_STEPS   = 8;
const float ATMOSPHERE_R_PLANET = 6371000.0;
const float ATMOSPHERE_R_ATMOS  = 6471000.0;
const vec3  ATMOSPHERE_K_RAYLEIGH = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float ATMOSPHERE_K_MIE_BASE = 21e-6;
const float ATMOSPHERE_H_RAYLEIGH = 8000.0;
const float ATMOSPHERE_H_MIE      = 1200.0;
const float ATMOSPHERE_MIE_G      = 0.758;
const float ATMOSPHERE_SUN_RADIANCE = 22.0;

// Ray-sphere intersection, sphere centred at origin, radius sr. Returns (near, far); near > far means miss.
vec2 raySphere(vec3 origin, vec3 dir, float sr)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - sr*sr;
    float d = b*b - c;
    if (d < 0.0) return vec2(1e9, -1e9);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}

// Light the atmosphere scatters toward rayOrigin along rayDir, for a sun in direction sunDir with radiance iSun and
// Mie scattering coefficient kMie (ATMOSPHERE_K_MIE_BASE scaled by the haze). No sun disc, no stars.
vec3 atmosphere(vec3 rayDir, vec3 rayOrigin, vec3 sunDir, float iSun, float kMie)
{
    vec2 atmos = raySphere(rayOrigin, rayDir, ATMOSPHERE_R_ATMOS);
    if (atmos.y < 0.0) return vec3(0.0);               // atmosphere entirely behind the viewer
    float tStart = max(atmos.x, 0.0);                  // the viewer sits inside the atmosphere
    float tEnd = atmos.y;
    // Only clamp the march to the ground when the ground is actually IN FRONT. For up-facing rays the planet
    // is behind (negative intersection); clamping to it unconditionally sends the march backward and yields a
    // black sky.
    vec2 planet = raySphere(rayOrigin, rayDir, ATMOSPHERE_R_PLANET);
    if (planet.x > 0.0) tEnd = min(tEnd, planet.x);
    float segLen = (tEnd - tStart)/float(ATMOSPHERE_PRIMARY_STEPS);
    float t = tStart;

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float odR = 0.0;
    float odM = 0.0;

    float mu = dot(rayDir, sunDir);
    float mumu = mu*mu;
    float gg = ATMOSPHERE_MIE_G*ATMOSPHERE_MIE_G;
    float phaseR = 3.0/(16.0*PI)*(1.0 + mumu);
    float phaseM = 3.0/(8.0*PI)*((1.0 - gg)*(mumu + 1.0))/(pow(1.0 + gg - 2.0*mu*ATMOSPHERE_MIE_G, 1.5)*(2.0 + gg));

    for (int i = 0; i < ATMOSPHERE_PRIMARY_STEPS; i++)
    {
        vec3 pos = rayOrigin + rayDir*(t + segLen*0.5);
        float h = length(pos) - ATMOSPHERE_R_PLANET;
        float hr = exp(-h/ATMOSPHERE_H_RAYLEIGH)*segLen;
        float hm = exp(-h/ATMOSPHERE_H_MIE)*segLen;
        odR += hr;
        odM += hm;

        vec2 lightAtmos = raySphere(pos, sunDir, ATMOSPHERE_R_ATMOS);
        float lSeg = lightAtmos.y/float(ATMOSPHERE_LIGHT_STEPS);
        float lt = 0.0;
        float lOdR = 0.0;
        float lOdM = 0.0;
        for (int j = 0; j < ATMOSPHERE_LIGHT_STEPS; j++)
        {
            vec3 lpos = pos + sunDir*(lt + lSeg*0.5);
            float lh = length(lpos) - ATMOSPHERE_R_PLANET;
            lOdR += exp(-lh/ATMOSPHERE_H_RAYLEIGH)*lSeg;
            lOdM += exp(-lh/ATMOSPHERE_H_MIE)*lSeg;
            lt += lSeg;
        }

        // Mie extinction is ~1.1x its scattering coefficient.
        vec3 tau = ATMOSPHERE_K_RAYLEIGH*(odR + lOdR) + kMie*1.1*(odM + lOdM);
        vec3 attn = exp(-tau);
        sumR += hr*attn;
        sumM += hm*attn;
        t += segLen;
    }

    return iSun*(phaseR*ATMOSPHERE_K_RAYLEIGH*sumR + phaseM*kMie*sumM);
}
