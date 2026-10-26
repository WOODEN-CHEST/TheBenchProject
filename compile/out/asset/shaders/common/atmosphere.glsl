// The atmosphere, for #include "common/atmosphere": the air (the Rayleigh scattering of the air itself, the haze's Mie
// scattering and absorption, the ozone layer's absorption) and the light it scatters toward the eye, lit by the sun by
// day and by the moon at night (the same model, a dimmer light). Two ways to get that light:
//  - THE TABLES (T15; Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020):
//    the atmosphere pass (source/WorldRendererAtmosphere.c) builds them first in every frame (the transmittance and the
//    multiple scattering only when the air changes: shaders/atmosphere/), and the sky pass, the fog and the stars read
//    them, with the light scattered more than once;
//  - THE DIRECT MARCH (atmosphere(), T34's: single scattering per pixel): the A/B path while the tables are switched
//    off, and the fallback where they are unavailable (atmosphereTablesActive false).
// World space (render space: the camera at the origin), +Y up, distances in metres; the planet's centre lies straight
// below the camera, atmosphereViewHeight beneath it plus the planet's radius.
//
// The tables' layout is mirrored in C by source/WorldAtmosphere.{h,c} (the same formulas in double): keep the two in
// step; tests/ShaderSourceTests.c pins the constants below to that file's macros.
//
// PRECISION. A float holds a radius of 6.4e6 m only to half a metre, and r*r - R*R near the ground loses most of its
// digits, so heights are carried as heights above the ground and the formulas below avoid that subtraction.

#include "common/math"

// ---- The planet and the air ------------------------------------------------------------------------------------
// The planet's radius and the top of its atmosphere (metres): WORLD_ATMOSPHERE_PLANET_RADIUS, _TOP_RADIUS.
const float ATMOSPHERE_R_PLANET = 6371000.0;
const float ATMOSPHERE_R_ATMOS = 6471000.0;
// The distance from the ground to the top of the atmosphere along the horizon: the longest ray the transmittance table
// holds from the ground.
const float ATMOSPHERE_TOP_FROM_HORIZON = sqrt((ATMOSPHERE_R_ATMOS - ATMOSPHERE_R_PLANET)*(ATMOSPHERE_R_ATMOS + ATMOSPHERE_R_PLANET));
// The air's (Rayleigh) scale height: its density falls by e every 8 km (WORLD_ATMOSPHERE_RAYLEIGH_HEIGHT).
const float ATMOSPHERE_RAYLEIGH_HEIGHT = 8000.0;
// The ozone layer, a tent from 10 to 40 km, densest at 25 km (WORLD_ATMOSPHERE_OZONE_CENTER, _HALF_WIDTH).
const float ATMOSPHERE_OZONE_CENTER = 25000.0;
const float ATMOSPHERE_OZONE_HALF_WIDTH = 15000.0;
// The sun's radiance on the air at sunIntensity 1, in the game's light units.
const float ATMOSPHERE_SUN_RADIANCE = 22.0;
// The angular diameter of the sun's disc (and the moon's), radians: the planet's shadow on the air is soft over it.
const float ATMOSPHERE_LIGHT_DIAMETER = 0.0093;
// Below this product of the horizontal lengths of a direction and a light, one of them is vertical and their azimuth
// undefined (WORLD_ATMOSPHERE_VERTICAL_EPSILON).
const float ATMOSPHERE_VERTICAL_EPSILON = 1.0e-9;

// The tables' sizes in texels (WORLD_ATMOSPHERE_TRANSMITTANCE_*, _MULTISCATTER_SIZE, _SKY_VIEW_*): the sky view is two
// halves of ATMOSPHERE_SKY_VIEW_HEIGHT rows, the sun's (rows 0 to 107) then the moon's.
const float ATMOSPHERE_TRANSMITTANCE_WIDTH = 256.0;
const float ATMOSPHERE_TRANSMITTANCE_HEIGHT = 64.0;
const float ATMOSPHERE_MULTISCATTER_SIZE = 32.0;
const float ATMOSPHERE_SKY_VIEW_WIDTH = 192.0;
const float ATMOSPHERE_SKY_VIEW_HEIGHT = 108.0;
const float ATMOSPHERE_SKY_VIEW_HALF_COUNT = 2.0;

// The direct march's steps (T34): 16 along the view ray, 8 toward the light.
const int ATMOSPHERE_PRIMARY_STEPS = 16;
const int ATMOSPHERE_LIGHT_STEPS = 8;

// The air this frame (WorldAtmosphere_GetMedium, from the environment's settings), per metre at sea level: the air's
// Rayleigh scattering (x air_density), the haze's scattering and extinction (sky_turbidity, haze_albedo), the haze's
// scale height in metres (haze_height) and its forward scattering (sun_glow, the phase function's g), and the ozone
// layer's absorption at its densest (x ozone).
uniform vec3 atmosphereRayleighScattering;
uniform vec3 atmosphereMieScattering;
uniform vec3 atmosphereMieExtinction;
uniform float atmosphereMieHeight;
uniform float atmosphereMieAnisotropy;
uniform vec3 atmosphereOzoneAbsorption;
// The camera's height above sea level, metres (at least 1 m: WorldAtmosphere_GetViewHeight): the tables' sky is seen
// from there, and the direct march starts there.
uniform float atmosphereViewHeight;
// Whether the tables hold this frame's sky: false while the direct march draws it (the A/B, the fallback).
uniform bool atmosphereTablesActive;
// The tables. The renderer keeps the transmittance and the sky view on fixed units for every reader
// (WorldRendererTextureUnit_AtmosphereTransmittance and _AtmosphereSkyView: 7 and 8); the multiple scattering table is
// read only while the sky view is built.
uniform sampler2D atmosphereTransmittanceTable;
uniform sampler2D atmosphereMultiscatterTable;
uniform sampler2D atmosphereSkyViewTable;


// ---- Geometry ---------------------------------------------------------------------------------------------------
// The height above the ground of the point `pathLength` metres along a ray that starts `height` metres up and climbs
// with the zenith cosine `cosZenith` (WorldAtmosphere_GetHeightAt): r^2 - R^2 is carried from the start's h (2R + h),
// never taken as a difference of two radii.
float atmosphereHeightAt(float height, float cosZenith, float pathLength)
{
    float radius = ATMOSPHERE_R_PLANET + height;
    float rhoSquared = height*(2.0*ATMOSPHERE_R_PLANET + height) + pathLength*(pathLength + 2.0*radius*cosZenith);
    return rhoSquared/(sqrt(max(rhoSquared + ATMOSPHERE_R_PLANET*ATMOSPHERE_R_PLANET, 0.0)) + ATMOSPHERE_R_PLANET);
}

// The distance from a point `height` metres up to the planet's axis through its horizon point, sqrt(r^2 - R^2).
float atmosphereHorizonDistance(float height)
{
    float h = max(height, 0.0);
    return sqrt(h*(2.0*ATMOSPHERE_R_PLANET + h));
}

// The zenith cosine of the horizon seen from `height` (WorldAtmosphere_GetHorizonCos): 0 on the ground, below 0 higher
// up; rays below it meet the ground.
float atmosphereHorizonCos(float height)
{
    return -atmosphereHorizonDistance(height)/(ATMOSPHERE_R_PLANET + max(height, 0.0));
}

// The distance along a ray to the top of the atmosphere (WorldAtmosphere_GetDistanceToTop).
float atmosphereDistanceToTop(float height, float cosZenith)
{
    float radius = ATMOSPHERE_R_PLANET + height;
    float topSquared = (ATMOSPHERE_R_ATMOS - radius)*(ATMOSPHERE_R_ATMOS + radius) + radius*radius*cosZenith*cosZenith;
    return max(-radius*cosZenith + sqrt(max(topSquared, 0.0)), 0.0);
}

// The distance along a ray to the ground, or -1 when it misses it (WorldAtmosphere_GetDistanceToGround).
float atmosphereDistanceToGround(float height, float cosZenith)
{
    float radius = ATMOSPHERE_R_PLANET + height;
    float h = max(height, 0.0);
    float discriminant = radius*radius*cosZenith*cosZenith - h*(2.0*ATMOSPHERE_R_PLANET + h);
    if ((cosZenith >= 0.0) || (discriminant < 0.0))
    {
        return -1.0;
    }
    return max(-radius*cosZenith - sqrt(discriminant), 0.0);
}


// ---- The air ----------------------------------------------------------------------------------------------------
float atmosphereOzoneDensity(float height)
{
    return max(1.0 - abs(height - ATMOSPHERE_OZONE_CENTER)/ATMOSPHERE_OZONE_HALF_WIDTH, 0.0);
}

// The air's extinction per metre at a height: its Rayleigh scattering, the haze's extinction, the ozone's absorption.
vec3 atmosphereExtinction(float height)
{
    return atmosphereRayleighScattering*exp(-height/ATMOSPHERE_RAYLEIGH_HEIGHT)
        + atmosphereMieExtinction*exp(-height/max(atmosphereMieHeight, 1.0))
        + atmosphereOzoneAbsorption*atmosphereOzoneDensity(height);
}

// Rayleigh's phase function, and the haze's (Cornette and Shanks) with the frame's forward scattering, for the cosine
// of the angle between the light and the view.
float atmosphereRayleighPhase(float cosAngle)
{
    return 3.0/(16.0*PI)*(1.0 + cosAngle*cosAngle);
}

float atmosphereMiePhase(float cosAngle)
{
    float g = atmosphereMieAnisotropy;
    float gg = g*g;
    return 3.0/(8.0*PI)*((1.0 - gg)*(cosAngle*cosAngle + 1.0))/(pow(1.0 + gg - 2.0*cosAngle*g, 1.5)*(2.0 + gg));
}

// The optical depth from a point `height` metres up along `cosZenith` to the top of the atmosphere: steps growing with
// the square of their index (short near the start, where the air is densest), each taken at its middle.
vec3 atmosphereOpticalDepth(float height, float cosZenith, int stepCount)
{
    float pathLength = atmosphereDistanceToTop(height, cosZenith);
    float steps = float(stepCount);
    vec3 depth = vec3(0.0);
    for (int i = 0; i < stepCount; i++)
    {
        float from = float(i)/steps;
        float to = float(i + 1)/steps;
        float stepLength = pathLength*(to*to - from*from);
        depth += atmosphereExtinction(atmosphereHeightAt(height, cosZenith, pathLength*from*from + 0.5*stepLength))
            *stepLength;
    }
    return depth;
}


// ---- The tables' layout -----------------------------------------------------------------------------------------
// A unit coordinate in [0, 1] across a table of `size` texels, from its first texel's centre to its last's, as a
// texture coordinate, and back (WorldAtmosphere_UnitToTexel, _TexelToUnit).
float atmosphereUnitToTexel(float unit, float size)
{
    return 0.5/size + unit*(size - 1.0)/size;
}

float atmosphereTexelToUnit(float coordinate, float size)
{
    return (coordinate - 0.5/size)*size/(size - 1.0);
}

// Where the transmittance table holds a ray (WorldAtmosphere_GetTransmittanceUv; Bruneton 2017): across by the distance
// to the top (straight up to the horizon), up by the distance to the planet's axis (dense near the ground). A ray below
// the horizon gets the horizon's column.
vec2 atmosphereTransmittanceUv(float height, float cosZenith)
{
    float h = max(height, 0.0);
    float rho = atmosphereHorizonDistance(h);
    float toTop = atmosphereDistanceToTop(h, cosZenith);
    float minToTop = (ATMOSPHERE_R_ATMOS - ATMOSPHERE_R_PLANET) - h;
    float maxToTop = rho + ATMOSPHERE_TOP_FROM_HORIZON;
    float across = clamp((toTop - minToTop)/(maxToTop - minToTop), 0.0, 1.0);
    float up = clamp(rho/ATMOSPHERE_TOP_FROM_HORIZON, 0.0, 1.0);
    return vec2(atmosphereUnitToTexel(across, ATMOSPHERE_TRANSMITTANCE_WIDTH),
        atmosphereUnitToTexel(up, ATMOSPHERE_TRANSMITTANCE_HEIGHT));
}

// The ray a point of the transmittance table holds (WorldAtmosphere_GetTransmittanceRay).
void atmosphereTransmittanceRay(vec2 uv, out float height, out float cosZenith)
{
    float across = atmosphereTexelToUnit(uv.x, ATMOSPHERE_TRANSMITTANCE_WIDTH);
    float up = atmosphereTexelToUnit(uv.y, ATMOSPHERE_TRANSMITTANCE_HEIGHT);
    float rho = ATMOSPHERE_TOP_FROM_HORIZON*up;
    float radius = sqrt(rho*rho + ATMOSPHERE_R_PLANET*ATMOSPHERE_R_PLANET);
    height = rho*rho/(radius + ATMOSPHERE_R_PLANET);
    float minToTop = (ATMOSPHERE_R_ATMOS - ATMOSPHERE_R_PLANET) - height;
    float maxToTop = rho + ATMOSPHERE_TOP_FROM_HORIZON;
    float toTop = minToTop + across*(maxToTop - minToTop);
    cosZenith = (toTop <= 0.0) ? 1.0 : clamp((ATMOSPHERE_TOP_FROM_HORIZON*ATMOSPHERE_TOP_FROM_HORIZON - rho*rho
        - toTop*toTop)/(2.0*radius*toTop), -1.0, 1.0);
}

// Where the multiple scattering table holds a point (WorldAtmosphere_GetMultiscatterUv): the light's zenith cosine
// across, the height up.
vec2 atmosphereMultiscatterUv(float height, float lightCosZenith)
{
    return vec2(atmosphereUnitToTexel(clamp(0.5*lightCosZenith + 0.5, 0.0, 1.0), ATMOSPHERE_MULTISCATTER_SIZE),
        atmosphereUnitToTexel(clamp(height/(ATMOSPHERE_R_ATMOS - ATMOSPHERE_R_PLANET), 0.0, 1.0),
            ATMOSPHERE_MULTISCATTER_SIZE));
}

// The point a texel of the multiple scattering table holds (WorldAtmosphere_GetMultiscatterRay).
void atmosphereMultiscatterRay(vec2 uv, out float height, out float lightCosZenith)
{
    lightCosZenith = 2.0*atmosphereTexelToUnit(uv.x, ATMOSPHERE_MULTISCATTER_SIZE) - 1.0;
    height = atmosphereTexelToUnit(uv.y, ATMOSPHERE_MULTISCATTER_SIZE)*(ATMOSPHERE_R_ATMOS - ATMOSPHERE_R_PLANET);
}

// The sky view's two angles from `viewHeight`: from the zenith to the horizon (beyond 90 degrees: the ground falls
// away), and from the horizon down to the nadir.
void atmosphereSkyViewHorizon(float viewHeight, out float zenithToHorizon, out float horizonToNadir)
{
    float h = max(viewHeight, 0.0);
    horizonToNadir = acos(min(atmosphereHorizonDistance(h)/(ATMOSPHERE_R_PLANET + h), 1.0));
    zenithToHorizon = PI - horizonToNadir;
}

// Where one half of the sky-view table holds a direction seen from `viewHeight`, in unit coordinates
// (WorldAtmosphere_GetSkyViewUnit; Hillaire's mapping): across the azimuth from the light, as the sine of half the angle
// (0.6 degrees a texel next to the light, where its glow is); up from the zenith to the horizon (0 to 0.5) and from
// the horizon to the nadir (0.5 to 1), each quadratic toward the horizon, where the sky changes fastest.
vec2 atmosphereSkyViewUnit(float viewHeight, float cosZenith, float cosLightAzimuth)
{
    float zenithToHorizon;
    float horizonToNadir;
    atmosphereSkyViewHorizon(viewHeight, zenithToHorizon, horizonToNadir);
    float zenithAngle = acos(clamp(cosZenith, -1.0, 1.0));
    float up = (zenithAngle <= zenithToHorizon)
        ? 0.5*(1.0 - sqrt(max(1.0 - zenithAngle/zenithToHorizon, 0.0)))
        : 0.5 + 0.5*sqrt(min((zenithAngle - zenithToHorizon)/horizonToNadir, 1.0));
    return vec2(sqrt(clamp(0.5*(1.0 - cosLightAzimuth), 0.0, 1.0)), up);
}

// The direction a point of one half of the sky-view table holds (WorldAtmosphere_GetSkyViewDirection).
void atmosphereSkyViewDirection(float viewHeight, vec2 unit, out float cosZenith, out float cosLightAzimuth)
{
    float zenithToHorizon;
    float horizonToNadir;
    atmosphereSkyViewHorizon(viewHeight, zenithToHorizon, horizonToNadir);
    float coordinate = (unit.y < 0.5) ? (1.0 - 2.0*unit.y) : (2.0*unit.y - 1.0);
    float zenithAngle = (unit.y < 0.5) ? zenithToHorizon*(1.0 - coordinate*coordinate)
        : zenithToHorizon + horizonToNadir*coordinate*coordinate;
    cosZenith = cos(zenithAngle);
    cosLightAzimuth = 1.0 - 2.0*unit.x*unit.x;
}

// The cosine of the azimuth between a direction and a light around world up (WorldAtmosphere_GetCosLightAzimuth): 1
// when either is vertical.
float atmosphereCosLightAzimuth(vec3 direction, vec3 lightDirection)
{
    float lengths = length(direction.xz)*length(lightDirection.xz);
    return (lengths > ATMOSPHERE_VERTICAL_EPSILON) ? clamp(dot(direction.xz, lightDirection.xz)/lengths, -1.0, 1.0)
        : 1.0;
}


// ---- Reading the tables -----------------------------------------------------------------------------------------
// The share of light that crosses the air from a point `height` metres up along `cosZenith` to the top of the
// atmosphere (the transmittance table). For a ray below the horizon it is the horizon's: the caller decides about the
// ground.
vec3 atmosphereTransmittanceToTop(float height, float cosZenith)
{
    return textureLod(atmosphereTransmittanceTable, atmosphereTransmittanceUv(height, cosZenith), 0.0).rgb;
}

// The light of the sun (or the moon) reaching a point `height` metres up whose zenith cosine toward the light is
// `cosZenith`: the transmittance table, in the planet's shadow once the light has set there, softly over the light's
// disc (a hard edge would band the twilight).
vec3 atmosphereLightTransmittance(float height, float cosZenith)
{
    float aboveHorizon = cosZenith - atmosphereHorizonCos(height);
    float shadow = smoothstep(-0.5*ATMOSPHERE_LIGHT_DIAMETER, 0.5*ATMOSPHERE_LIGHT_DIAMETER, aboveHorizon);
    return atmosphereTransmittanceToTop(height, cosZenith)*shadow;
}

// The light scattered twice or more toward a point, for a light of radiance 1 at the zenith cosine `lightCosZenith`
// there, per unit of the air's scattering (the multiple scattering table).
vec3 atmosphereMultipleScattering(float height, float lightCosZenith)
{
    return textureLod(atmosphereMultiscatterTable, atmosphereMultiscatterUv(height, lightCosZenith), 0.0).rgb;
}

// The sky's light toward a direction from one half of the sky-view table (0 the sun's, with light pollution's
// skyglow; 1 the moon's), as the atmosphere pass built it for this frame: below the horizon, for now, the darkened
// mirror of the sky above it (T35 draws the far ground there). `direction` is a unit vector.
vec3 atmosphereSkyView(vec3 direction, vec3 lightDirection, float tableHalf)
{
    vec2 unit = atmosphereSkyViewUnit(atmosphereViewHeight, direction.y,
        atmosphereCosLightAzimuth(direction, lightDirection));
    vec2 uv = vec2(atmosphereUnitToTexel(unit.x, ATMOSPHERE_SKY_VIEW_WIDTH),
        (tableHalf + atmosphereUnitToTexel(unit.y, ATMOSPHERE_SKY_VIEW_HEIGHT))/ATMOSPHERE_SKY_VIEW_HALF_COUNT);
    return textureLod(atmosphereSkyViewTable, uv, 0.0).rgb;
}


// ---- The tables' march ------------------------------------------------------------------------------------------
// (1 - e^-x) / x per channel: the share of a step's own light that leaves it, 1 for a clear step (and its series where
// x is too small to divide by).
vec3 atmosphereStepShare(vec3 depth)
{
    vec3 share = (vec3(1.0) - exp(-depth))/max(depth, vec3(1e-6));
    // A boolean select, not a blend by 0 or 1: a step too thick to cross (an infinite depth, in absurdly thick haze)
    // gives a share of 0, where the blend's 0 x infinity would give NaN.
    return mix(share, vec3(1.0) - 0.5*depth, lessThanEqual(depth, vec3(1e-3)));
}

// The light the air scatters toward a viewer `height` metres up, looking along a ray with the zenith cosine
// `cosZenith`, out to `maxLength` metres, the top of the atmosphere or the ground, for a light of radiance 1 whose
// zenith cosine at the viewer is `lightCosZenith` (`cosLightAngle`: the cosine between the ray and the light): its
// single scattering (each step's light through the transmittance table, in the planet's shadow where the light has set)
// plus `multipleScattering` of its multiple scattering (the table). The steps grow with the square of their index
// (short near the viewer, where every ray meets the densest air; a level ray rises with the square of its length as the
// planet curves away), and each step's light is integrated exactly over its own air and dimmed by the air before it.
// Also gives the ray's transmittance.
vec3 atmosphereScatteredLight(float height, float cosZenith, float lightCosZenith, float cosLightAngle,
    float maxLength, int stepCount, float multipleScattering, out vec3 transmittance)
{
    float radius = ATMOSPHERE_R_PLANET + height;
    float pathLength = atmosphereDistanceToTop(height, cosZenith);
    float toGround = atmosphereDistanceToGround(height, cosZenith);
    if (toGround >= 0.0)
    {
        pathLength = min(pathLength, toGround);
    }
    pathLength = min(pathLength, maxLength);
    float phaseR = atmosphereRayleighPhase(cosLightAngle);
    float phaseM = atmosphereMiePhase(cosLightAngle);
    float mieHeight = max(atmosphereMieHeight, 1.0);
    float steps = float(stepCount);
    vec3 light = vec3(0.0);
    vec3 depth = vec3(0.0);
    for (int i = 0; i < stepCount; i++)
    {
        float from = float(i)/steps;
        float to = float(i + 1)/steps;
        float stepLength = pathLength*(to*to - from*from);
        float t = pathLength*from*from + 0.5*stepLength;
        float h = atmosphereHeightAt(height, cosZenith, t);
        float lightCos = clamp((radius*lightCosZenith + t*cosLightAngle)/(ATMOSPHERE_R_PLANET + h), -1.0, 1.0);
        vec3 scatteringR = atmosphereRayleighScattering*exp(-h/ATMOSPHERE_RAYLEIGH_HEIGHT);
        float densityM = exp(-h/mieHeight);
        vec3 scatteringM = atmosphereMieScattering*densityM;
        vec3 extinction = scatteringR + atmosphereMieExtinction*densityM
            + atmosphereOzoneAbsorption*atmosphereOzoneDensity(h);
        vec3 source = (scatteringR*phaseR + scatteringM*phaseM)*atmosphereLightTransmittance(h, lightCos)
            + (scatteringR + scatteringM)*atmosphereMultipleScattering(h, lightCos)*multipleScattering;
        vec3 stepDepth = extinction*stepLength;
        light += exp(-depth)*source*stepLength*atmosphereStepShare(stepDepth);
        depth += stepDepth;
    }
    transmittance = exp(-depth);
    return light;
}


// ---- The direct march (T34): the A/B path and the fallback -----------------------------------------------------
// TODO: remove the direct march (here, and its branches in the sky pass, fogSkyColor and the stars) with the A/B switch
// once the owner approves the tables' sky (T15; WorldRenderer_SetAtmosphereTablesEnabled).
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

// Light the atmosphere scatters toward the camera along rayDir, for a light (the sun, or the moon) in direction sunDir
// with radiance iSun, through the frame's air, single scattering only, marched per pixel. No discs, no stars. Also gives
// the view ray's transmittance: the share of what lies beyond the air along rayDir (the moon's disc) that reaches the
// viewer.
vec3 atmosphere(vec3 rayDir, vec3 sunDir, float iSun, out vec3 transmittance)
{
    transmittance = vec3(1.0);
    vec3 rayOrigin = vec3(0.0, ATMOSPHERE_R_PLANET + atmosphereViewHeight, 0.0);
    vec2 atmos = raySphere(rayOrigin, rayDir, ATMOSPHERE_R_ATMOS);
    if (atmos.y < 0.0) return vec3(0.0);               // atmosphere entirely behind the viewer
    float tStart = max(atmos.x, 0.0);                  // the viewer sits inside the atmosphere
    float tEnd = atmos.y;
    // Only clamp the march to the ground when the ground is actually IN FRONT. For up-facing rays the planet
    // is behind (negative intersection); clamping to it unconditionally sends the march backward and yields a
    // black sky.
    vec2 planet = raySphere(rayOrigin, rayDir, ATMOSPHERE_R_PLANET);
    if (planet.x > 0.0) tEnd = min(tEnd, planet.x);
    float span = tEnd - tStart;

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float odR = 0.0;
    float odM = 0.0;
    float odO = 0.0;

    float mu = dot(rayDir, sunDir);
    float phaseR = atmosphereRayleighPhase(mu);
    float phaseM = atmosphereMiePhase(mu);
    float mieHeight = max(atmosphereMieHeight, 1.0);

    // The steps grow with the square of their index: short near the viewer, where every ray meets the densest air (a
    // level ray rises with the square of its distance as the planet curves away). Equal steps were about 70 km long
    // on a level ray, so the low air the horizon's light comes from fell inside the first one, and each step's light
    // was dimmed as if it all came from the step's far end: the horizon came out at a quarter to a third of the
    // brightness three degrees higher, a dark brown band, and the sky as a whole about 30 % off. Measured against 1024
    // steps, this march is about 1 % off on average and 6 % at worst at the default haze, for 8 % more time than the old
    // one (T34; integrating each step's light exactly was no more accurate and took 22 % more).
    for (int i = 0; i < ATMOSPHERE_PRIMARY_STEPS; i++)
    {
        float stepFrom = float(i)/float(ATMOSPHERE_PRIMARY_STEPS);
        float stepTo = float(i + 1)/float(ATMOSPHERE_PRIMARY_STEPS);
        float segLen = span*(stepTo*stepTo - stepFrom*stepFrom);
        vec3 pos = rayOrigin + rayDir*(tStart + span*stepFrom*stepFrom + segLen*0.5);
        float h = length(pos) - ATMOSPHERE_R_PLANET;
        float densityR = exp(-h/ATMOSPHERE_RAYLEIGH_HEIGHT);
        float densityM = exp(-h/mieHeight);
        float densityO = atmosphereOzoneDensity(h);

        vec2 lightAtmos = raySphere(pos, sunDir, ATMOSPHERE_R_ATMOS);
        float lSeg = lightAtmos.y/float(ATMOSPHERE_LIGHT_STEPS);
        float lt = 0.0;
        float lOdR = 0.0;
        float lOdM = 0.0;
        float lOdO = 0.0;
        for (int j = 0; j < ATMOSPHERE_LIGHT_STEPS; j++)
        {
            vec3 lpos = pos + sunDir*(lt + lSeg*0.5);
            float lh = length(lpos) - ATMOSPHERE_R_PLANET;
            lOdR += exp(-lh/ATMOSPHERE_RAYLEIGH_HEIGHT)*lSeg;
            lOdM += exp(-lh/mieHeight)*lSeg;
            lOdO += atmosphereOzoneDensity(lh)*lSeg;
            lt += lSeg;
        }

        // The step's light reaches the viewer dimmed by the air in front of the step (odR, odM, odO: the steps
        // before), by half the step's own air (its light is scattered all along the step, so on average from its
        // middle) and by the light's own path to the sun.
        float halfR = 0.5*densityR*segLen;
        float halfM = 0.5*densityM*segLen;
        float halfO = 0.5*densityO*segLen;
        vec3 tau = atmosphereRayleighScattering*(odR + halfR + lOdR) + atmosphereMieExtinction*(odM + halfM + lOdM)
            + atmosphereOzoneAbsorption*(odO + halfO + lOdO);
        vec3 attn = exp(-tau)*segLen;
        sumR += densityR*attn;
        sumM += densityM*attn;
        odR += densityR*segLen;
        odM += densityM*segLen;
        odO += densityO*segLen;
    }

    transmittance = exp(-(atmosphereRayleighScattering*odR + atmosphereMieExtinction*odM + atmosphereOzoneAbsorption*odO));
    return iSun*(phaseR*atmosphereRayleighScattering*sumR + phaseM*atmosphereMieScattering*sumM);
}

// The same without the transmittance.
vec3 atmosphere(vec3 rayDir, vec3 sunDir, float iSun)
{
    vec3 transmittance;
    return atmosphere(rayDir, sunDir, iSun, transmittance);
}
