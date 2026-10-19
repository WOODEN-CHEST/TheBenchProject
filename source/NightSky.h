#pragma once
#include <stddef.h>
#include <stdint.h>
// raylib.h supplies Vector3, Color and Matrix, which the star records and the galactic frame use.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"


/*
 * Private header (source/): the night sky's pure maths, used by the renderer's star and sky passes and by the tests:
 * the seeded star catalogue, the faintest star that can still be seen (light pollution and twilight), the air mass
 * toward an altitude, star colours, and the fixed orientation of the galaxy among the stars. No GL, no state.
 *
 * FRAMES. Stars are placed in the star frame of WorldEnvironment_GetStarsToWorld: the celestial frame (the pole above
 * the horizon at -z) of the moment the sun stands at right ascension 90 degrees, the June solstice. The renderer turns
 * it into the world every frame, with the stars' daily turn and their drift through the year. NightSky_GetCelestialToGalactic
 * places the galaxy in that frame as on Earth: its plane tilted 62.9 degrees to the celestial equator, its centre at
 * declination -29 degrees, so on a June night the galactic centre crosses the meridian at midnight and in December it
 * stays below the horizon at night.
 *
 * MAGNITUDES. Brightness is in astronomical magnitudes: lower is brighter, 5 magnitudes are a factor of 100. A star's
 * flux relative to magnitude 0 is 10^(-0.4 m).
 */


// Macros.
/** @brief Stars of an Earth-like sky (StarDensity 1): about the count the naked eye sees from a dark site. */
#define NIGHT_SKY_EARTH_LIKE_STAR_COUNT 9000.0f
/** @brief Most stars one catalogue may hold, whatever the density. */
#define NIGHT_SKY_MAX_STAR_COUNT ((size_t)20000)
/** @brief Brightest catalogue magnitude (Sirius is -1.5). */
#define NIGHT_SKY_BRIGHTEST_MAGNITUDE (-1.0f)
/** @brief Faintest catalogue magnitude (just past the dark-site limit, so the faintest stars can fade in). */
#define NIGHT_SKY_FAINTEST_MAGNITUDE 7.0f
/** @brief How much more numerous stars get per magnitude fainter (the number brighter than m grows x2.8 per magnitude). */
#define NIGHT_SKY_STARS_PER_MAGNITUDE_RATIO 2.8
/** @brief The faintest magnitude a dark site shows overhead at full night. */
#define NIGHT_SKY_DARK_SITE_LIMITING_MAGNITUDE 6.5f
/** @brief Magnitudes full light pollution (1) takes off the limiting magnitude (6.5 - 3.5 = 3: a city's few stars). */
#define NIGHT_SKY_LIGHT_POLLUTION_MAGNITUDES 3.5f
/** @brief Limiting magnitude while the sun is on or above the horizon: brighter than any star, so none show. */
#define NIGHT_SKY_DAYTIME_LIMITING_MAGNITUDE (-1.5f)
/** @brief How much the limiting magnitude grows per degree the sun sinks below the horizon: magnitude 1.5 at -6
 *  degrees (only the brightest stars), 4.5 at -12, beyond the dark-site limit at -18 (full night). */
#define NIGHT_SKY_TWILIGHT_MAGNITUDES_PER_DEGREE 0.5f
/** @brief Limiting magnitudes between which the Milky Way fades in: gone at or below the first, full at the second. */
#define NIGHT_SKY_MILKY_WAY_HIDDEN_LIMIT 4.5f
#define NIGHT_SKY_MILKY_WAY_FULL_LIMIT 6.0f
/** @brief Magnitudes a full moon high in the sky (moonlit sky 1) takes off a dark site's limit: 6.5 becomes 4.75, so
 *  the faint stars and most of the Milky Way are gone. Light pollution leaves the moon less to take (see
 *  NightSky_GetLimitingMagnitude). */
#define NIGHT_SKY_FULL_MOON_MAGNITUDES 1.75f
/** @brief The brightest moonlit sky NightSky_GetLimitingMagnitude takes: 16 times a full moon's at the default
 *  moonlight. */
#define NIGHT_SKY_MAX_MOONLIT_SKY 16.0f


// Types.
/**
 * @brief One star of a generated catalogue.
 *
 * A plain value produced by NightSky_GenerateStars; it owns nothing.
 */
typedef struct NightSkyStarStruct
{
    /** @brief Unit direction in the celestial frame (see the file comment). */
    Vector3 Direction;
    /** @brief Apparent magnitude, within [NIGHT_SKY_BRIGHTEST_MAGNITUDE, NIGHT_SKY_FAINTEST_MAGNITUDE]. */
    float Magnitude;
    /** @brief Flux relative to magnitude 0: 10^(-0.4 * Magnitude). */
    float Flux;
    /** @brief B-V colour index (-0.35 blue-white .. 2.0 orange-red). */
    float ColorIndex;
    /** @brief The colour of its temperature as sRGB, brightest channel 255, alpha 255. */
    Color Color;
    /** @brief Per-star random value in [0, 1) that desynchronizes its twinkling. */
    float TwinkleSeed;
} NightSkyStar;


// Functions.
/**
 * @brief Returns how many stars a catalogue of @p density holds: NIGHT_SKY_EARTH_LIKE_STAR_COUNT x density, rounded,
 *        at most NIGHT_SKY_MAX_STAR_COUNT; 0 for a density that is not a positive finite number.
 * @param density The environment's StarDensity (1 = Earth-like).
 * @returns The star count.
 */
size_t NightSky_GetStarCount(float density);

/**
 * @brief Generates the star catalogue of @p seed and @p density and appends it to @p outStars.
 *
 * Deterministic: the same seed and density always give the same stars, on every platform. Directions are uniform on
 * the sphere, up to twice as dense near the galactic plane (NightSky_GetCelestialToGalactic), so the stars crowd the
 * Milky Way as on Earth. Magnitudes follow the count law of real stars (each magnitude fainter has 2.8 times as many)
 * between NIGHT_SKY_BRIGHTEST_MAGNITUDE and NIGHT_SKY_FAINTEST_MAGNITUDE. Colours follow a B-V distribution of mostly
 * white to yellow stars with some blue and some orange ones (NightSky_GetColorTemperature, then
 * NightSky_GetTemperatureColor). Does not clear @p outStars first.
 * @param seed The environment's StarSeed.
 * @param density The environment's StarDensity (see NightSky_GetStarCount).
 * @param outStars [out] Buffer of NightSkyStar elements the stars are appended to. Must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p outStars is NULL or its element size is not sizeof(NightSkyStar);
 *          ErrorCode_BufferTooLarge if it cannot grow.
 */
Error NightSky_GenerateStars(uint64_t seed, float density, GenericBuffer* outStars);

/**
 * @brief Returns the faintest magnitude visible overhead for a light pollution, a moonlit sky and a sun elevation.
 *
 * The smaller of two limits:
 * - the night limit: NIGHT_SKY_DARK_SITE_LIMITING_MAGNITUDE, minus NIGHT_SKY_LIGHT_POLLUTION_MAGNITUDES x
 *   lightPollution, minus the moon's share: NIGHT_SKY_FULL_MOON_MAGNITUDES x log10(1 + 20 m) / log10(21) for the
 *   moonlit sky m, times (1 - lightPollution). The logarithm follows the eye: a quarter moon (a tenth of the full
 *   moon's light) still takes about 0.6 magnitudes; and an already bright city sky leaves the moon less to take;
 * - the twilight limit: NIGHT_SKY_DAYTIME_LIMITING_MAGNITUDE with the sun up, growing
 *   NIGHT_SKY_TWILIGHT_MAGNITUDES_PER_DEGREE per degree below the horizon. So at dusk the brightest stars appear first,
 *   and full night begins at about -18 degrees.
 * @param lightPollution The environment's LightPollution; clamped to [0, 1] (NaN counts as 0).
 * @param moonlitSky How much the moon brightens the sky: 1 for a full moon high up at the default moonlight
 *        (WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY), 0 for no moon; clamped to [0, NIGHT_SKY_MAX_MOONLIT_SKY] (NaN counts
 *        as 0).
 * @param sunElevationDegrees The sun's elevation above the horizon, in degrees; a non-finite value counts as day.
 * @returns The limiting magnitude; stars fainter than it are not drawn (they fade out within 0.5 magnitudes of it).
 */
float NightSky_GetLimitingMagnitude(float lightPollution, float moonlitSky, float sunElevationDegrees);

/**
 * @brief Returns how visible the Milky Way is (0..1) at a limiting magnitude: 0 at or below
 *        NIGHT_SKY_MILKY_WAY_HIDDEN_LIMIT, 1 at or above NIGHT_SKY_MILKY_WAY_FULL_LIMIT, smooth in between.
 * @param limitingMagnitude From NightSky_GetLimitingMagnitude.
 * @returns The visibility factor in [0, 1].
 */
float NightSky_GetMilkyWayVisibility(float limitingMagnitude);

/**
 * @brief Returns the air mass toward a direction: how much atmosphere its light crosses, relative to straight up.
 *
 * Kasten and Young (1989): X = 1 / (cos z + 0.50572 (96.07995 - z)^-1.6364), z = the zenith angle in degrees. 1 at the
 * zenith, about 2 at 30 degrees altitude and 38 at the horizon. Directions below the horizon count as on it. The star
 * and sky shaders use the same formula (shaders/common/night_sky.glsl) for extinction near the horizon.
 * @param sinAltitude The sine of the altitude (a world direction's y component).
 * @returns The air mass, >= 1.
 */
float NightSky_GetAirmass(float sinAltitude);

/**
 * @brief Converts a B-V colour index to a colour temperature in kelvin (Ballesteros 2012):
 *        T = 4600 (1 / (0.92 BV + 1.7) + 1 / (0.92 BV + 0.62)).
 * @param colorIndex The B-V index; clamped to [-0.4, 2.0].
 * @returns The temperature in kelvin (about 2900 to 22000).
 */
float NightSky_GetColorTemperature(float colorIndex);

/**
 * @brief Returns the sRGB colour of a black body at a temperature (Tanner Helland's fit), scaled so its brightest
 *        channel is 255. Red-orange at 3000 K, white near 6600 K, blue-white above.
 * @param kelvin The temperature; clamped to [1000, 40000].
 * @returns The colour, alpha 255.
 */
Color NightSky_GetTemperatureColor(float kelvin);

/**
 * @brief Returns the fixed rotation from the star frame to galactic coordinates (x toward the galactic centre,
 *        z toward the north galactic pole): the J2000 equatorial-to-galactic rotation, placed in the star frame
 *        as described in the file comment. Apply it with Vector3Transform.
 * @returns The rotation (orthonormal, no translation).
 */
Matrix NightSky_GetCelestialToGalactic(void);
