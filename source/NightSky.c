#include <math.h>
#include "NightSky.h"
#include "raylib/raymath.h"
#include "wr/WRRandom.h"


// Macros.
#define NIGHT_SKY_PI 3.14159265358979323846
/** The sun's right ascension, in degrees, that sets the season of the sky: 90 is a June night, with the galactic
 *  centre on the meridian at midnight. */
// TODO: T03 derives the season from the in-game date (T23) once the calendar exists.
#define NIGHT_SKY_SUN_RIGHT_ASCENSION_DEGREES 90.0f
/** Width of the band in which stars crowd the galactic plane, as the sine of the galactic latitude (about 15 deg). */
#define NIGHT_SKY_GALACTIC_CROWDING_WIDTH 0.26
/** Most direction draws one star takes before its last draw is kept regardless of the crowding test. */
#define NIGHT_SKY_MAX_DIRECTION_ATTEMPTS 64
/** B-V range of the catalogue, and the range NightSky_GetColorTemperature accepts. */
#define NIGHT_SKY_MIN_CATALOGUE_COLOR_INDEX (-0.35)
#define NIGHT_SKY_MAX_COLOR_INDEX 2.0
#define NIGHT_SKY_MIN_TEMPERATURE_COLOR_INDEX (-0.4)
/** Temperature range NightSky_GetTemperatureColor accepts, in kelvin. */
#define NIGHT_SKY_MIN_KELVIN 1000.0
#define NIGHT_SKY_MAX_KELVIN 40000.0
/** Odd constant (2^64 / golden ratio) that spreads star indices over the seed space. */
#define NIGHT_SKY_INDEX_SPREAD 0x9E3779B97F4A7C15ULL
/** Largest value of an 8-bit colour channel. */
#define NIGHT_SKY_COLOR_CHANNEL_MAX 255.0


// Types.
/* One population of the B-V mixture: its share of the stars and its normal distribution. */
typedef struct ColorPopulationStruct
{
    double Share;
    double Mean;
    double Deviation;
} ColorPopulation;


// Fields.
/* The B-V mixture: mostly white to yellow stars, some blue and some orange ones. The shares add up to 1. */
static const ColorPopulation ColorPopulations[] =
{
    { 0.15, -0.15, 0.08 }, // blue-white (B and early A stars)
    { 0.35, 0.15, 0.15 },  // white (A and F)
    { 0.30, 0.65, 0.15 },  // yellow (G)
    { 0.20, 1.20, 0.20 },  // orange (K giants)
};


// Static functions.
static double ClampDouble(double value, double minimum, double maximum)
{
    if (!(value >= minimum)) { return minimum; } // also catches NaN
    if (value > maximum) { return maximum; }
    return value;
}

/* SplitMix64: the recommended way to expand one 64-bit seed into xoshiro256** state. */
static uint64_t SplitMix64Next(uint64_t* state)
{
    *state += NIGHT_SKY_INDEX_SPREAD;
    uint64_t Mixed = *state;
    Mixed = (Mixed ^ (Mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    Mixed = (Mixed ^ (Mixed >> 27)) * 0x94D049BB133111EBULL;
    return Mixed ^ (Mixed >> 31);
}

/* Each star draws from its own stream, derived from the seed and its index. A star whose draws round differently on
 * another platform therefore cannot change the stars after it, and a denser catalogue starts with the same stars as a
 * sparser one. */
static void ConstructStarRandom(Random* random, uint64_t seed, size_t index)
{
    uint64_t State = seed ^ ((uint64_t)index * NIGHT_SKY_INDEX_SPREAD);
    RandomState Seeded;
    for (int Word = 0; Word < RANDOM_STATE_LENGTH; Word++)
    {
        Seeded.Values[Word] = SplitMix64Next(&State);
    }
    Random_Construct2(random, Seeded);
}

/* A standard normal value (Box-Muller). */
static double NextGaussian(Random* random)
{
    double Radius = sqrt(-2.0 * log(1.0 - Random_NextDouble(random))); // 1 - [0,1) never reaches log(0)
    return Radius * cos(2.0 * NIGHT_SKY_PI * Random_NextDouble(random));
}

/* A uniform direction on the sphere, kept with probability (1 + crowding) / 2, where the crowding is 1 on the galactic
 * plane and falls off away from it: up to twice as many stars near the plane. */
static Vector3 NextDirection(Random* random, Matrix celestialToGalactic)
{
    Vector3 Direction = { 0.0f, 0.0f, 1.0f };
    for (int Attempt = 0; Attempt < NIGHT_SKY_MAX_DIRECTION_ATTEMPTS; Attempt++)
    {
        double Z = 2.0 * Random_NextDouble(random) - 1.0;
        double Azimuth = 2.0 * NIGHT_SKY_PI * Random_NextDouble(random);
        double Radius = sqrt(fmax(0.0, 1.0 - Z * Z));
        Direction = (Vector3){ (float)(Radius * cos(Azimuth)), (float)(Radius * sin(Azimuth)), (float)Z };

        double Crowding = (double)Vector3Transform(Direction, celestialToGalactic).z / NIGHT_SKY_GALACTIC_CROWDING_WIDTH;
        double KeepChance = 0.5 * (1.0 + exp(-(Crowding * Crowding)));
        if (Random_NextDouble(random) < KeepChance)
        {
            break;
        }
    }
    return Direction;
}

/* Inverse CDF of the star count law N(<m) ~ 2.8^m between the brightest and faintest catalogue magnitudes. */
static float NextMagnitude(Random* random)
{
    double LowCount = pow(NIGHT_SKY_STARS_PER_MAGNITUDE_RATIO, (double)NIGHT_SKY_BRIGHTEST_MAGNITUDE);
    double HighCount = pow(NIGHT_SKY_STARS_PER_MAGNITUDE_RATIO, (double)NIGHT_SKY_FAINTEST_MAGNITUDE);
    double Count = LowCount + Random_NextDouble(random) * (HighCount - LowCount);
    double Magnitude = log(Count) / log(NIGHT_SKY_STARS_PER_MAGNITUDE_RATIO);
    return (float)ClampDouble(Magnitude, (double)NIGHT_SKY_BRIGHTEST_MAGNITUDE, (double)NIGHT_SKY_FAINTEST_MAGNITUDE);
}

/* A B-V index from the population mixture. */
static float NextColorIndex(Random* random)
{
    double Pick = Random_NextDouble(random);
    size_t Count = sizeof(ColorPopulations) / sizeof(ColorPopulations[0]);
    const ColorPopulation* Population = &ColorPopulations[Count - 1U];
    for (size_t Index = 0U; Index < Count; Index++)
    {
        if (Pick < ColorPopulations[Index].Share)
        {
            Population = &ColorPopulations[Index];
            break;
        }
        Pick -= ColorPopulations[Index].Share;
    }
    double ColorIndex = Population->Mean + Population->Deviation * NextGaussian(random);
    return (float)ClampDouble(ColorIndex, NIGHT_SKY_MIN_CATALOGUE_COLOR_INDEX, NIGHT_SKY_MAX_COLOR_INDEX);
}

static unsigned char ToChannel(double value, double scale)
{
    return (unsigned char)lround(ClampDouble(value * scale, 0.0, NIGHT_SKY_COLOR_CHANNEL_MAX));
}


// Public functions.
size_t NightSky_GetStarCount(float density)
{
    if (!isfinite(density) || (density <= 0.0f))
    {
        return 0U;
    }
    double Count = round((double)NIGHT_SKY_EARTH_LIKE_STAR_COUNT * (double)density);
    if (Count >= (double)NIGHT_SKY_MAX_STAR_COUNT)
    {
        return NIGHT_SKY_MAX_STAR_COUNT;
    }
    return (size_t)Count;
}

Error NightSky_GenerateStars(uint64_t seed, float density, GenericBuffer* outStars)
{
    if ((outStars == NULL) || (outStars->_elementSize != sizeof(NightSkyStar)))
    {
        return Error_Construct2(ErrorCode_IllegalArgument,
            "NightSky_GenerateStars: outStars must be a buffer of NightSkyStar elements.");
    }
    size_t Count = NightSky_GetStarCount(density);
    if (!GenericBuffer_ReserveMoreCapacity(outStars, Count))
    {
        return Error_Construct2(ErrorCode_BufferTooLarge, "NightSky_GenerateStars: the star buffer cannot grow.");
    }

    Matrix CelestialToGalactic = NightSky_GetCelestialToGalactic();
    for (size_t Index = 0U; Index < Count; Index++)
    {
        Random StarRandom;
        ConstructStarRandom(&StarRandom, seed, Index);
        NightSkyStar Star;
        Star.Direction = NextDirection(&StarRandom, CelestialToGalactic);
        Star.Magnitude = NextMagnitude(&StarRandom);
        Star.Flux = powf(10.0f, -0.4f * Star.Magnitude);
        Star.ColorIndex = NextColorIndex(&StarRandom);
        Star.Color = NightSky_GetTemperatureColor(NightSky_GetColorTemperature(Star.ColorIndex));
        Star.TwinkleSeed = Random_NextSingle(&StarRandom);
        Random_Deconstruct1(&StarRandom);
        if (!GenericBuffer_AddLast(outStars, &Star))
        {
            return Error_Construct2(ErrorCode_BufferTooLarge, "NightSky_GenerateStars: the star buffer cannot grow.");
        }
    }
    return Error_CreateSuccess();
}

float NightSky_GetLimitingMagnitude(float lightPollution, float sunElevationDegrees)
{
    double Pollution = ClampDouble((double)lightPollution, 0.0, 1.0);
    double NightLimit = (double)NIGHT_SKY_DARK_SITE_LIMITING_MAGNITUDE - (double)NIGHT_SKY_LIGHT_POLLUTION_MAGNITUDES * Pollution;
    if (!isfinite(sunElevationDegrees))
    {
        return NIGHT_SKY_DAYTIME_LIMITING_MAGNITUDE;
    }
    double DegreesBelowHorizon = fmax(0.0, -(double)sunElevationDegrees);
    double TwilightLimit = (double)NIGHT_SKY_DAYTIME_LIMITING_MAGNITUDE
        + (double)NIGHT_SKY_TWILIGHT_MAGNITUDES_PER_DEGREE * DegreesBelowHorizon;
    return (float)fmin(NightLimit, TwilightLimit);
}

float NightSky_GetMilkyWayVisibility(float limitingMagnitude)
{
    double Fade = ((double)limitingMagnitude - (double)NIGHT_SKY_MILKY_WAY_HIDDEN_LIMIT)
        / ((double)NIGHT_SKY_MILKY_WAY_FULL_LIMIT - (double)NIGHT_SKY_MILKY_WAY_HIDDEN_LIMIT);
    Fade = ClampDouble(Fade, 0.0, 1.0);
    return (float)(Fade * Fade * (3.0 - 2.0 * Fade));
}

float NightSky_GetAirmass(float sinAltitude)
{
    double CosZenith = ClampDouble((double)sinAltitude, 0.0, 1.0);
    double ZenithDegrees = acos(CosZenith) * 180.0 / NIGHT_SKY_PI;
    return (float)(1.0 / (CosZenith + 0.50572 * pow(96.07995 - ZenithDegrees, -1.6364)));
}

float NightSky_GetColorTemperature(float colorIndex)
{
    double Index = ClampDouble((double)colorIndex, NIGHT_SKY_MIN_TEMPERATURE_COLOR_INDEX, NIGHT_SKY_MAX_COLOR_INDEX);
    return (float)(4600.0 * ((1.0 / ((0.92 * Index) + 1.7)) + (1.0 / ((0.92 * Index) + 0.62))));
}

Color NightSky_GetTemperatureColor(float kelvin)
{
    // Tanner Helland's fit of the black-body colour, in units of 100 K; it gives sRGB channels in 0..255.
    double Temperature = ClampDouble((double)kelvin, NIGHT_SKY_MIN_KELVIN, NIGHT_SKY_MAX_KELVIN) / 100.0;
    double Red = (Temperature <= 66.0) ? 255.0 : 329.698727446 * pow(Temperature - 60.0, -0.1332047592);
    double Green = (Temperature <= 66.0) ? (99.4708025861 * log(Temperature) - 161.1195681661)
        : (288.1221695283 * pow(Temperature - 60.0, -0.0755148492));
    double Blue = (Temperature >= 66.0) ? 255.0
        : ((Temperature <= 19.0) ? 0.0 : (138.5177312231 * log(Temperature - 10.0) - 305.0447927307));

    Red = ClampDouble(Red, 0.0, NIGHT_SKY_COLOR_CHANNEL_MAX);
    Green = ClampDouble(Green, 0.0, NIGHT_SKY_COLOR_CHANNEL_MAX);
    Blue = ClampDouble(Blue, 0.0, NIGHT_SKY_COLOR_CHANNEL_MAX);
    double Brightest = fmax(Red, fmax(Green, Blue));
    double Scale = (Brightest > 0.0) ? (NIGHT_SKY_COLOR_CHANNEL_MAX / Brightest) : 1.0;
    return (Color){ .r = ToChannel(Red, Scale), .g = ToChannel(Green, Scale), .b = ToChannel(Blue, Scale), .a = 255 };
}

Matrix NightSky_GetCelestialToGalactic(void)
{
    // The celestial frame has the pole above the horizon at -z, so equatorial coordinates (z = north celestial pole,
    // x = right ascension 0) are the celestial ones turned half a turn about x, then turned about the pole so the sun
    // (celestial +x) sits at its right ascension.
    Matrix CelestialToEquatorial = MatrixMultiply(MatrixScale(1.0f, -1.0f, -1.0f),
        MatrixRotateZ(NIGHT_SKY_SUN_RIGHT_ASCENSION_DEGREES * DEG2RAD));

    // J2000 equatorial to galactic (Hipparcos): rows are the galactic x (toward the centre), y and z (north pole)
    // axes in equatorial coordinates. raylib's Matrix stores row r in m[r], m[r+4], m[r+8], m[r+12].
    Matrix EquatorialToGalactic = MatrixIdentity();
    EquatorialToGalactic.m0 = -0.0548755604f; EquatorialToGalactic.m4 = -0.8734370902f; EquatorialToGalactic.m8 = -0.4838350155f;
    EquatorialToGalactic.m1 = 0.4941094279f;  EquatorialToGalactic.m5 = -0.4448296300f; EquatorialToGalactic.m9 = 0.7469822445f;
    EquatorialToGalactic.m2 = -0.8676661490f; EquatorialToGalactic.m6 = -0.1980763734f; EquatorialToGalactic.m10 = 0.4559837762f;

    // raymath's MatrixMultiply(a, b) applies a first.
    return MatrixMultiply(CelestialToEquatorial, EquatorialToGalactic);
}
