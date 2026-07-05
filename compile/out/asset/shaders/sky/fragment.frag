#version 330

// Full-screen atmospheric sky: Rayleigh + Mie single-scattering, plus a sun disc and a night star field.
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

uniform float starSeed;        // seed offsetting the star field
uniform float starDensity;     // relative star density
uniform float starBrightness;  // star brightness multiplier

out vec4 finalColor;

const float PI = 3.141592653589793;

// Atmosphere constants (metres).
const int   PRIMARY_STEPS = 16;
const int   LIGHT_STEPS   = 8;
const float R_PLANET = 6371000.0;
const float R_ATMOS  = 6471000.0;
const vec3  K_RAYLEIGH = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float K_MIE_BASE = 21e-6;
const float H_RAYLEIGH = 8000.0;
const float H_MIE      = 1200.0;
const float MIE_G      = 0.758;
const float SUN_RADIANCE = 22.0;

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

vec3 atmosphere(vec3 rayDir, vec3 rayOrigin, vec3 sunDir, float iSun, float kMie)
{
    vec2 atmos = raySphere(rayOrigin, rayDir, R_ATMOS);
    if (atmos.y < 0.0) return vec3(0.0);               // atmosphere entirely behind the viewer
    float tStart = max(atmos.x, 0.0);                  // the viewer sits inside the atmosphere
    float tEnd = atmos.y;
    // Only clamp the march to the ground when the ground is actually IN FRONT. For up-facing rays the planet
    // is behind (negative intersection); clamping to it unconditionally sends the march backward and yields a
    // black sky.
    vec2 planet = raySphere(rayOrigin, rayDir, R_PLANET);
    if (planet.x > 0.0) tEnd = min(tEnd, planet.x);
    float segLen = (tEnd - tStart)/float(PRIMARY_STEPS);
    float t = tStart;

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float odR = 0.0;
    float odM = 0.0;

    float mu = dot(rayDir, sunDir);
    float mumu = mu*mu;
    float gg = MIE_G*MIE_G;
    float phaseR = 3.0/(16.0*PI)*(1.0 + mumu);
    float phaseM = 3.0/(8.0*PI)*((1.0 - gg)*(mumu + 1.0))/(pow(1.0 + gg - 2.0*mu*MIE_G, 1.5)*(2.0 + gg));

    for (int i = 0; i < PRIMARY_STEPS; i++)
    {
        vec3 pos = rayOrigin + rayDir*(t + segLen*0.5);
        float h = length(pos) - R_PLANET;
        float hr = exp(-h/H_RAYLEIGH)*segLen;
        float hm = exp(-h/H_MIE)*segLen;
        odR += hr;
        odM += hm;

        vec2 lightAtmos = raySphere(pos, sunDir, R_ATMOS);
        float lSeg = lightAtmos.y/float(LIGHT_STEPS);
        float lt = 0.0;
        float lOdR = 0.0;
        float lOdM = 0.0;
        for (int j = 0; j < LIGHT_STEPS; j++)
        {
            vec3 lpos = pos + sunDir*(lt + lSeg*0.5);
            float lh = length(lpos) - R_PLANET;
            lOdR += exp(-lh/H_RAYLEIGH)*lSeg;
            lOdM += exp(-lh/H_MIE)*lSeg;
            lt += lSeg;
        }

        // Mie extinction is ~1.1x its scattering coefficient.
        vec3 tau = K_RAYLEIGH*(odR + lOdR) + kMie*1.1*(odM + lOdM);
        vec3 attn = exp(-tau);
        sumR += hr*attn;
        sumM += hm*attn;
        t += segLen;
    }

    return iSun*(phaseR*K_RAYLEIGH*sumR + phaseM*kMie*sumM);
}

float hash13(vec3 p)
{
    p = fract(p*0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y)*p.z);
}

// Sparse star field hashed directly from the 3D view ray, so stars are spread uniformly across the whole
// dome (the previous atan/asin mapping was singular at the zenith and clustered every star near the horizon).
// No horizon gate: stars cover the entire sphere so the sky reads as an infinite starfield.
vec3 stars(vec3 rayDir, float seed, float density, float brightness)
{
    vec3 cell = floor(rayDir*300.0 + seed);
    float present = hash13(cell);
    float threshold = 1.0 - clamp(density, 0.0, 1.0)*0.02;
    if (present < threshold) return vec3(0.0);
    float bright = hash13(cell + 3.7);
    return vec3(max(brightness, 0.0)*2.0*(0.4 + 0.6*bright));
}

void main()
{
    vec2 uv = gl_FragCoord.xy/resolution;
    vec4 clip = vec4(uv*2.0 - 1.0, 1.0, 1.0);
    vec4 world = invViewProj*clip;
    vec3 rayDir = normalize(world.xyz/world.w - cameraPos);

    // Ground viewer just above the planet surface; the sky dome uses world +Y as up.
    vec3 rayOrigin = vec3(0.0, R_PLANET + 1.0, 0.0);
    vec3 sunDir = normalize(sunDirection);

    float kMie = K_MIE_BASE*max(turbidity, 0.0)/3.0;
    float iSun = SUN_RADIANCE*max(sunIntensity, 0.0);

    // Infinite-skybox look: there is no ground. Compute the atmosphere with the ray mirrored into the upper
    // hemisphere (abs(y)) so every direction gets a sky colour, then darken below the horizon so it still
    // reads as "down" instead of a mirror-bright ground.
    vec3 skyRay = vec3(rayDir.x, abs(rayDir.y), rayDir.z);
    vec3 sky = atmosphere(skyRay, rayOrigin, sunDir, iSun, kMie);
    float belowFactor = smoothstep(0.0, -0.12, rayDir.y); // 0 above the horizon, 1 below
    sky *= mix(1.0, 0.5, belowFactor);

    // Sun disc, only above the horizon (so the mirror does not create a second sun below).
    float discCos = cos(radians(0.5*max(sunSize, 0.01)));
    if ((rayDir.y > -0.02) && (dot(rayDir, sunDir) > discCos))
    {
        sky += max(sunColor, vec3(0.0))*iSun;
    }

    // Stars cover the whole dome (above and below the horizon), fading in as the sun drops below the horizon.
    float night = clamp(-sunDir.y*4.0, 0.0, 1.0);
    sky += stars(rayDir, starSeed, starDensity, starBrightness)*night;

    sky *= max(skyTint, vec3(0.0));
    finalColor = vec4(sky, 1.0); // linear HDR; tonemap post-pass maps to display
}
