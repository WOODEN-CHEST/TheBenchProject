// The atmosphere, for #include "common/atmosphere": the air (the Rayleigh scattering of the air itself, the haze's Mie
// scattering and absorption, the ozone layer's absorption) and the light it scatters toward the eye, lit by the sun by
// day and by the moon at night (the same model, a dimmer light). Two ways to get that light:
//  - THE TABLES (Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020):
//    the atmosphere pass (source/WorldRendererAtmosphere.c) builds them first in every frame (the transmittance and the
//    multiple scattering only when the air changes: shaders/atmosphere/), and the sky pass and the stars read them, with
//    the light scattered more than once. The sky-view table ends its rays below the horizon on the far ground,
//    and the pass also builds the aerial-perspective volume from them (the haze in front of every surface: its layout
//    is here, its readers' side in common/haze);
//  - THE DIRECT MARCH (atmosphere(): single scattering per pixel): the A/B path while the tables are switched
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
// The sun's light above the air on a surface facing it, at sun_intensity 1 and a white sun_color, in the game's light
// units, the surfaces' and the sky's alike (WORLD_ATMOSPHERE_SUN_LIGHT: chosen so the sun 60 degrees up lights the
// ground at sea level with a luminance of 0.911). A light's intensity uniform (its colour in it) multiplies it.
const float ATMOSPHERE_SUN_LIGHT = 1.078;
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

// The direct march's steps: 16 along the view ray, 8 toward the light.
const int ATMOSPHERE_PRIMARY_STEPS = 16;
const int ATMOSPHERE_LIGHT_STEPS = 8;

// The aerial-perspective volume (WORLD_ATMOSPHERE_VOLUME_*): 32 by 32 screen directions at 32 slices, the slices'
// distances growing by a fixed ratio from the first (1 m) to the last distance slice (200 km), then one for the whole
// ray to the top of the atmosphere. One texture holds it: the slices side by side, the in-scatter's tiles in the lower
// rows, the transmittance's above them, and one row of the frame's values on top.
const float ATMOSPHERE_VOLUME_SIZE = 32.0;
const float ATMOSPHERE_VOLUME_SLICE_COUNT = 32.0;
const float ATMOSPHERE_VOLUME_NEAREST = 1.0;
const float ATMOSPHERE_VOLUME_FARTHEST = 200000.0;
const float ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE = ATMOSPHERE_VOLUME_SLICE_COUNT - 2.0;
const float ATMOSPHERE_VOLUME_WIDTH = ATMOSPHERE_VOLUME_SIZE*ATMOSPHERE_VOLUME_SLICE_COUNT;
const float ATMOSPHERE_VOLUME_HEIGHT = 2.0*ATMOSPHERE_VOLUME_SIZE + 1.0;
// The steps of each slice's march: within this distance, beyond it, and for the whole ray (the C mirror's comment has
// their accuracy).
const float ATMOSPHERE_VOLUME_NEAR_DISTANCE = 4096.0;
const int ATMOSPHERE_VOLUME_NEAR_STEPS = 8;
const int ATMOSPHERE_VOLUME_FAR_STEPS = 24;
const int ATMOSPHERE_VOLUME_RAY_STEPS = 32;
// The frame's values in the volume's last row, by column (WorldAtmosphereVolumeValue): the light at the ground haze's
// base, per unit of its scattering: the sun's and the moon's (to be multiplied by the haze's phase toward them), and the
// light from all around (the sky's and light pollution's).
const int ATMOSPHERE_VOLUME_GROUND_HAZE_SUN = 0;
const int ATMOSPHERE_VOLUME_GROUND_HAZE_MOON = 1;
const int ATMOSPHERE_VOLUME_GROUND_HAZE_AMBIENT = 2;
// The sky's light by height in the volume's last row (WORLD_ATMOSPHERE_SKY_LIGHT_*, WorldAtmosphereSkyLight): this
// many heights from the ground to the highest, even in the height's square root, a block of columns each for the sun's
// light through the air, the moon's, the sky's irradiance on a surface facing up and the ground's bounce on one facing
// down (the light that lights a surface at that height), from the first column on.
const int ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT = 64;
const float ATMOSPHERE_SKY_LIGHT_HIGHEST = 20000.0;
const int ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN = 64;
const int ATMOSPHERE_SKY_LIGHT_SUN = 0;
const int ATMOSPHERE_SKY_LIGHT_MOON = 1;
const int ATMOSPHERE_SKY_LIGHT_SKY = 2;
const int ATMOSPHERE_SKY_LIGHT_GROUND = 3;
const int ATMOSPHERE_SKY_LIGHT_COUNT = 4;
// The sky's irradiance table (WORLD_ATMOSPHERE_IRRADIANCE_*): the light the sky sends a surface facing up per unit of
// a light above the air, by the light's elevation across (from the lowest to the zenith, even in
// asinh(elevation / ELEVATION_SCALE): dense at the horizon) and the height up (even in log(1 + height / HEIGHT_SCALE):
// even in the height near the ground), then one column of light pollution's irradiance per unit of its radiance; each
// texel the logarithm of its value (at least the floor), so the bilinear read follows light falling off exponentially.
const float ATMOSPHERE_IRRADIANCE_WIDTH = 64.0;
const float ATMOSPHERE_IRRADIANCE_HEIGHT = 32.0;
const float ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH = ATMOSPHERE_IRRADIANCE_WIDTH + 1.0;
const float ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION = -0.3490658503988659;
const float ATMOSPHERE_IRRADIANCE_LOG_FLOOR = -46.0;
const float ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE = 0.07;
const float ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE = 1000.0;
// Light pollution's light in the air: the town's light fills the air below about this height and within about this
// horizontal distance of the camera (metres; WORLD_ATMOSPHERE_LIGHT_POLLUTION_HEIGHT, _REACH).
const float ATMOSPHERE_LIGHT_POLLUTION_HEIGHT = 500.0;
const float ATMOSPHERE_LIGHT_POLLUTION_REACH = 10000.0;
// How far a ray of the sky goes through the ground haze, in metres (WORLD_ATMOSPHERE_SKY_DISTANCE).
const float ATMOSPHERE_SKY_DISTANCE = 100000000.0;

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
// The sky's irradiance table, read while the sky view and the volume are built.
uniform sampler2D atmosphereIrradianceTable;


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

// The air at a point `height` metres up: its Rayleigh and haze scattering and its extinction per metre. A point below the
// ground (a ray of the haze passes through it: the world's ground is flat and goes on where the planet curves away)
// has the air on the ground.
void atmosphereAirAt(float height, out vec3 scatteringR, out vec3 scatteringM, out vec3 extinction)
{
    float h = max(height, 0.0);
    scatteringR = atmosphereRayleighScattering*exp(-h/ATMOSPHERE_RAYLEIGH_HEIGHT);
    float densityM = exp(-h/max(atmosphereMieHeight, 1.0));
    scatteringM = atmosphereMieScattering*densityM;
    extinction = scatteringR + atmosphereMieExtinction*densityM + atmosphereOzoneAbsorption*atmosphereOzoneDensity(h);
}

// How much of light pollution's light (the town's, scattered by the air) the air holds at a point `height` metres up
// (below the ground: as on it) and `horizontalDistance` from the camera, 1 at the camera's foot
// (WorldAtmosphere_GetLightPollutionShare).
float atmosphereLightPollutionShare(float height, float horizontalDistance)
{
    return exp(-max(height, 0.0)/ATMOSPHERE_LIGHT_POLLUTION_HEIGHT)
        *exp(-max(horizontalDistance, 0.0)/ATMOSPHERE_LIGHT_POLLUTION_REACH);
}

// The air's extinction per metre at a height: its Rayleigh scattering, the haze's extinction, the ozone's absorption.
vec3 atmosphereExtinction(float height)
{
    return atmosphereRayleighScattering*exp(-height/ATMOSPHERE_RAYLEIGH_HEIGHT)
        + atmosphereMieExtinction*exp(-height/max(atmosphereMieHeight, 1.0))
        + atmosphereOzoneAbsorption*atmosphereOzoneDensity(height);
}

// Rayleigh's phase function, Cornette and Shanks's for a forward scattering g, and the haze's (Cornette and Shanks's with
// the frame's forward scattering), for the cosine of the angle between the light and the view.
float atmosphereRayleighPhase(float cosAngle)
{
    return 3.0/(16.0*PI)*(1.0 + cosAngle*cosAngle);
}

float atmosphereCornetteShanksPhase(float cosAngle, float g)
{
    float gg = g*g;
    return 3.0/(8.0*PI)*((1.0 - gg)*(cosAngle*cosAngle + 1.0))/(pow(1.0 + gg - 2.0*cosAngle*g, 1.5)*(2.0 + gg));
}

float atmosphereMiePhase(float cosAngle)
{
    return atmosphereCornetteShanksPhase(cosAngle, atmosphereMieAnisotropy);
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

// The sky's light toward a direction from one half of the sky-view table (0 the sun's, with light pollution's glow and
// the far ground's ambient; 1 the moon's), as the atmosphere pass built it for this frame: below the horizon, the far
// ground seen through the haze. `direction` is a unit vector.
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

// The light one light of radiance 1 scatters per metre toward the viewer at a point `height` metres up (below the ground:
// as on it), its zenith cosine there `lightCos`: its single scattering through the transmittance table (`phaseR` and
// `phaseM`, the phase functions for the angle between the view and the light), in the planet's shadow once the light
// has set there, plus `multipleScattering` of the multiple scattering table's glow. The air's scattering at the point
// is `scatteringR` and `scatteringM` (atmosphereAirAt).
vec3 atmosphereLightSource(float height, float lightCos, float phaseR, float phaseM, vec3 scatteringR, vec3 scatteringM,
    float multipleScattering)
{
    float h = max(height, 0.0);
    return (scatteringR*phaseR + scatteringM*phaseM)*atmosphereLightTransmittance(h, lightCos)
        + (scatteringR + scatteringM)*atmosphereMultipleScattering(h, lightCos)*multipleScattering;
}

// The light the air scatters toward a viewer `height` metres up along the first `pathLength` metres of a ray with the
// zenith cosine `cosZenith`, through the ground if the ray reaches it (the air below it as on it: the haze's rays; the
// sky's stop at the ground or the top themselves), for a light of radiance `lightRadiance` (its light above the air,
// coloured) whose zenith cosine at the viewer is `lightCosZenith` (`cosLightAngle`: the cosine between the ray and the
// light), plus light pollution's
// `lightPollution` (the radiance of the town's light in the air at the camera's foot; 0: none). Each point's light is
// atmosphereLightSource's, the light's zenith there its own on the curved planet. The steps grow with the square of their
// index (short near the viewer, where every ray meets the densest air; a level ray rises with the square of its length
// as the planet curves away), and each step's light is integrated exactly over its own air and dimmed by the air before
// it. Also gives the path's transmittance.
vec3 atmosphereScatteredLight(float height, float cosZenith, float lightCosZenith, float cosLightAngle,
    float pathLength, int stepCount, vec3 lightRadiance, float multipleScattering, vec3 lightPollution,
    out vec3 transmittance)
{
    float radius = ATMOSPHERE_R_PLANET + height;
    float horizontal = sqrt(max(1.0 - cosZenith*cosZenith, 0.0));
    float phaseR = atmosphereRayleighPhase(cosLightAngle);
    float phaseM = atmosphereMiePhase(cosLightAngle);
    bool hasLight = any(greaterThan(lightRadiance, vec3(0.0)));
    bool hasPollution = any(greaterThan(lightPollution, vec3(0.0)));
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
        vec3 scatteringR;
        vec3 scatteringM;
        vec3 extinction;
        atmosphereAirAt(h, scatteringR, scatteringM, extinction);
        vec3 source = vec3(0.0);
        if (hasLight)
        {
            float lightCos = clamp((radius*lightCosZenith + t*cosLightAngle)/(ATMOSPHERE_R_PLANET + h), -1.0, 1.0);
            source = lightRadiance*atmosphereLightSource(h, lightCos, phaseR, phaseM, scatteringR, scatteringM,
                multipleScattering);
        }
        if (hasPollution)
        {
            source += (scatteringR + scatteringM)*lightPollution*atmosphereLightPollutionShare(h, t*horizontal);
        }
        vec3 stepDepth = extinction*stepLength;
        light += exp(-depth)*source*stepLength*atmosphereStepShare(stepDepth);
        depth += stepDepth;
    }
    transmittance = exp(-depth);
    return light;
}


// ---- The aerial-perspective volume's layout ---------------------------------------------------------------------
// The distance of one of the volume's distance slices, in metres (WorldAtmosphere_GetVolumeSliceDistance).
float atmosphereVolumeSliceDistance(float slice)
{
    return ATMOSPHERE_VOLUME_NEAREST*pow(ATMOSPHERE_VOLUME_FARTHEST/ATMOSPHERE_VOLUME_NEAREST,
        clamp(slice, 0.0, ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE)/ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE);
}

// Where along the volume's slices a distance lies (WorldAtmosphere_GetVolumeSlice): -1 at the camera, rising linearly
// to 0 at the first slice; between two distance slices the lower one's index plus the share of the way to the next,
// linear in distance; past the last distance slice, linearly on toward the whole ray's slice, reached at the top of the
// atmosphere (`distanceToTop`, the ray's distance to it, through the ground).
float atmosphereVolumeSlice(float distance, float distanceToTop)
{
    if (!(distance > 0.0))
    {
        return -1.0;
    }
    if (distance < ATMOSPHERE_VOLUME_NEAREST)
    {
        return -1.0 + distance/ATMOSPHERE_VOLUME_NEAREST;
    }
    if (distance <= ATMOSPHERE_VOLUME_FARTHEST)
    {
        // The slice below the distance (its rounding does not matter: the share of the way to the next slice makes up
        // for it), then the share of the way.
        float index = log(distance/ATMOSPHERE_VOLUME_NEAREST)*ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE
            /log(ATMOSPHERE_VOLUME_FARTHEST/ATMOSPHERE_VOLUME_NEAREST);
        float lower = clamp(floor(index), 0.0, ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE - 1.0);
        float from = atmosphereVolumeSliceDistance(lower);
        float to = atmosphereVolumeSliceDistance(lower + 1.0);
        return lower + clamp((distance - from)/(to - from), 0.0, 1.0);
    }
    float remaining = distanceToTop - ATMOSPHERE_VOLUME_FARTHEST;
    float share = (remaining > 0.0) ? min((distance - ATMOSPHERE_VOLUME_FARTHEST)/remaining, 1.0) : 1.0;
    return ATMOSPHERE_VOLUME_LAST_DISTANCE_SLICE + share;
}

// Where the volume's texture holds a screen direction's value at one slice (WorldAtmosphere_GetVolumeUv): `screenUv` in
// [0, 1] (0 at the scene's bottom left), `part` 0 the in-scatter's tile, 1 the transmittance's; kept half a texel inside
// the tile, so bilinear sampling never reads the neighbouring slice.
vec2 atmosphereVolumeUv(vec2 screenUv, float slice, float part)
{
    vec2 inTile = clamp(screenUv*ATMOSPHERE_VOLUME_SIZE, vec2(0.5), vec2(ATMOSPHERE_VOLUME_SIZE - 0.5));
    return vec2((slice*ATMOSPHERE_VOLUME_SIZE + inTile.x)/ATMOSPHERE_VOLUME_WIDTH,
        (part*ATMOSPHERE_VOLUME_SIZE + inTile.y)/ATMOSPHERE_VOLUME_HEIGHT);
}

// The texel of one of the frame's values in the volume's last row (WorldAtmosphere_GetVolumeValueUv), for texelFetch.
ivec2 atmosphereVolumeValueTexel(int value)
{
    return ivec2(value, int(2.0*ATMOSPHERE_VOLUME_SIZE));
}


// ---- The sky's light by height and the irradiance table ---------------------------------------------------------
// A height's place among the sky's light's heights (WorldAtmosphere_GetSkyLightCoordinate): (count - 1) x sqrt(height /
// highest), clamped; the readers blend the two whole coordinates around it.
float atmosphereSkyLightCoordinate(float height)
{
    return float(ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1)*sqrt(clamp(height/ATMOSPHERE_SKY_LIGHT_HIGHEST, 0.0, 1.0));
}

// The height a whole coordinate stands for (WorldAtmosphere_GetSkyLightHeight).
float atmosphereSkyLightHeight(int index)
{
    float unit = float(index)/float(ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1);
    return ATMOSPHERE_SKY_LIGHT_HIGHEST*unit*unit;
}

// The texel of one value of the sky's light at one of its heights (WorldAtmosphere_GetSkyLightColumn), for texelFetch.
ivec2 atmosphereSkyLightTexel(int value, int index)
{
    return ivec2(ATMOSPHERE_SKY_LIGHT_FIRST_COLUMN + value*ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT
        + clamp(index, 0, ATMOSPHERE_SKY_LIGHT_HEIGHT_COUNT - 1), int(2.0*ATMOSPHERE_VOLUME_SIZE));
}

// Where the irradiance table holds a height and a light's zenith cosine (WorldAtmosphere_GetIrradianceUnit, _Uv), as
// texture coordinates in its lights' columns, and light pollution's column at a height
// (WorldAtmosphere_GetLightPollutionIrradianceUv).
// The mappings' ends: the asinh of the lowest elevation's and the zenith's over the elevation scale, and the log of the
// top's height over the height scale.
const float ATMOSPHERE_IRRADIANCE_LOWEST_ACROSS = asinh(ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION
    /ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE);
const float ATMOSPHERE_IRRADIANCE_HIGHEST_ACROSS = asinh(0.5*PI/ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE);
const float ATMOSPHERE_IRRADIANCE_TOP_UP = log(1.0 + (ATMOSPHERE_R_ATMOS - ATMOSPHERE_R_PLANET)
    /ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE);

float atmosphereIrradianceRowUnit(float height)
{
    return clamp(log(1.0 + max(height, 0.0)/ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE)/ATMOSPHERE_IRRADIANCE_TOP_UP, 0.0, 1.0);
}

float atmosphereIrradianceColumnUnit(float lightCosZenith)
{
    float elevation = asin(clamp(lightCosZenith, -1.0, 1.0));
    return clamp((asinh(elevation/ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE) - ATMOSPHERE_IRRADIANCE_LOWEST_ACROSS)
        /(ATMOSPHERE_IRRADIANCE_HIGHEST_ACROSS - ATMOSPHERE_IRRADIANCE_LOWEST_ACROSS), 0.0, 1.0);
}

// The height and the light's elevation (radians) a unit row and column hold (WorldAtmosphere_GetIrradianceRay).
float atmosphereIrradianceRowHeight(float up)
{
    return ATMOSPHERE_IRRADIANCE_HEIGHT_SCALE*(exp(up*ATMOSPHERE_IRRADIANCE_TOP_UP) - 1.0);
}

float atmosphereIrradianceColumnElevation(float across)
{
    return ATMOSPHERE_IRRADIANCE_ELEVATION_SCALE*sinh(ATMOSPHERE_IRRADIANCE_LOWEST_ACROSS
        + across*(ATMOSPHERE_IRRADIANCE_HIGHEST_ACROSS - ATMOSPHERE_IRRADIANCE_LOWEST_ACROSS));
}

vec2 atmosphereIrradianceUv(float height, float lightCosZenith)
{
    return vec2((0.5 + atmosphereIrradianceColumnUnit(lightCosZenith)*(ATMOSPHERE_IRRADIANCE_WIDTH - 1.0))
        /ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH,
        atmosphereUnitToTexel(atmosphereIrradianceRowUnit(height), ATMOSPHERE_IRRADIANCE_HEIGHT));
}

vec2 atmosphereLightPollutionIrradianceUv(float height)
{
    return vec2((ATMOSPHERE_IRRADIANCE_WIDTH + 0.5)/ATMOSPHERE_IRRADIANCE_TEXTURE_WIDTH,
        atmosphereUnitToTexel(atmosphereIrradianceRowUnit(height), ATMOSPHERE_IRRADIANCE_HEIGHT));
}

// The sky's irradiance on a surface facing up `height` metres up, per unit of a light above the air at the zenith cosine
// `lightCosZenith` (untinted; none for a light below the table's lowest elevation, as the C mirror's
// WorldSkyLight_GetSkyIrradiance), and light pollution's per unit of its radiance at the camera's foot.
vec3 atmosphereSkyIrradiance(float height, float lightCosZenith)
{
    if (lightCosZenith < sin(ATMOSPHERE_IRRADIANCE_LOWEST_ELEVATION))
    {
        return vec3(0.0);
    }
    return exp(textureLod(atmosphereIrradianceTable, atmosphereIrradianceUv(height, lightCosZenith), 0.0).rgb);
}

vec3 atmosphereLightPollutionIrradiance(float height)
{
    return exp(textureLod(atmosphereIrradianceTable, atmosphereLightPollutionIrradianceUv(height), 0.0).rgb);
}


// ---- The direct march: the A/B path and the fallback -----------------------------------------------------------
// TODO: remove the direct march (here, and its branches in the sky pass and the stars) with the A/B switch
// once the owner approves the tables' sky (WorldRenderer_SetAtmosphereTablesEnabled).
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
// with radiance iSun (its light above the air, coloured), through the frame's air, single scattering only, marched per
// pixel. No discs, no stars. Also gives the view ray's transmittance: the share of what lies beyond the air along rayDir
// (the moon's disc) that reaches the viewer.
vec3 atmosphere(vec3 rayDir, vec3 sunDir, vec3 iSun, out vec3 transmittance)
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
    // level ray rises with the square of its distance as the planet curves away). Equal steps would be about 70 km
    // long on a level ray, so the low air the horizon's light comes from would fall inside the first one; with each
    // step's light dimmed as if it all came from the step's far end, that gives a horizon a quarter to a third as
    // bright as the sky three degrees higher, a dark brown band, and a sky about 30 % off as a whole. Measured against
    // 1024 steps, this march is about 1 % off on average and 6 % at worst at the default haze, for 8 % more time than a
    // march of equal steps (integrating each step's light exactly is no more accurate and takes 22 % more).
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
vec3 atmosphere(vec3 rayDir, vec3 sunDir, vec3 iSun)
{
    vec3 transmittance;
    return atmosphere(rayDir, sunDir, iSun, transmittance);
}
