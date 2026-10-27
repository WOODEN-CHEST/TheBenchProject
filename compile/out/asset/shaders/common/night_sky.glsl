// The night sky's shared maths, for #include "common/night_sky": the air mass and extinction that dim stars and the
// Milky Way near the horizon, and the profile of light pollution's skyglow (the sky pass's direct march draws it; the
// tables hold light pollution's light in the air instead). The CPU twin of nightSkyAirmass is NightSky_GetAirmass
// (source/NightSky.c), which the tests check.

// Magnitudes lost per air mass beyond the zenith's (a clear sky): a star at 10 degrees altitude loses about 1.1, one at
// the horizon all of its light.
const float NIGHT_SKY_EXTINCTION_PER_AIRMASS = 0.25;
// Skyglow overhead, as a share of its strength at the horizon, and how fast it fades with altitude (in sine units).
const float NIGHT_SKY_SKYGLOW_ZENITH_SHARE = 0.06;
const float NIGHT_SKY_SKYGLOW_FALLOFF = 0.12;

// Kasten and Young (1989): the air mass toward a direction with this sine of altitude, 1 at the zenith and about 38 at
// the horizon. Directions below the horizon count as on it.
float nightSkyAirmass(float sinAltitude)
{
    float cosZenith = clamp(sinAltitude, 0.0, 1.0);
    float zenithDegrees = degrees(acos(cosZenith));
    return 1.0/(cosZenith + 0.50572*pow(96.07995 - zenithDegrees, -1.6364));
}

// The share of a light's brightness extinction leaves at an air mass, relative to the zenith: 10^(-0.4 k (X - 1)).
float nightSkyExtinction(float airmass)
{
    return exp2(-1.3287712*NIGHT_SKY_EXTINCTION_PER_AIRMASS*(airmass - 1.0));
}

// Light pollution's skyglow toward a direction with this sine of altitude: horizonRadiance at the horizon, fading with
// altitude to NIGHT_SKY_SKYGLOW_ZENITH_SHARE of it overhead. The renderer scales horizonRadiance by the light pollution
// and fades it out by day.
vec3 nightSkySkyglow(float sinAltitude, vec3 horizonRadiance)
{
    float altitude = max(sinAltitude, 0.0);
    float profile = NIGHT_SKY_SKYGLOW_ZENITH_SHARE
        + (1.0 - NIGHT_SKY_SKYGLOW_ZENITH_SHARE)*exp(-altitude/NIGHT_SKY_SKYGLOW_FALLOFF);
    return horizonRadiance*profile;
}
