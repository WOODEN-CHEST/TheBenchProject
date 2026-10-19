#include "TestSuites.h"
#include "NightSky.h"
#include "WorldEnvironment.h"
#include "GameCalendar.h"
#include "raylib/raymath.h"
#include <math.h>


// Macros.
/** Seed of the catalogues these tests generate. */
#define TEST_STAR_SEED ((uint64_t)7)
/** Degrees per radian, for readable angle checks. */
#define TEST_DEGREES (180.0 / 3.14159265358979323846)


// Static functions.
/* Generates the catalogue of @p seed and @p density into a fresh buffer; free its _data afterwards. */
static bool GenerateCatalogue(TestRun* run, uint64_t seed, float density, GenericBuffer* outStars)
{
    GenericBuffer_AllocateVariable(outStars, 0U, sizeof(NightSkyStar));
    return TEST_CHECK_SUCCESS(run, NightSky_GenerateStars(seed, density, outStars));
}

static const NightSkyStar* StarsOf(const GenericBuffer* buffer)
{
    const void* Data = buffer->_data;
    return Data;
}


// Tests: the catalogue.
static void CatalogueIsDeterministicPerSeed(TestRun* run)
{
    GenericBuffer First;
    GenericBuffer Again;
    GenericBuffer Other;
    if (GenerateCatalogue(run, TEST_STAR_SEED, 0.5f, &First) && GenerateCatalogue(run, TEST_STAR_SEED, 0.5f, &Again)
        && GenerateCatalogue(run, TEST_STAR_SEED + 1U, 0.5f, &Other))
    {
        TEST_CHECK(run, (First._count == Again._count) && (First._count == Other._count));
        TEST_CHECK(run, Memory_IsEqual(First._data, Again._data, First._count * sizeof(NightSkyStar)));

        // Another seed gives other stars (directions agreeing to the last bit would be a broken generator).
        size_t SameDirections = 0U;
        for (size_t Index = 0U; Index < First._count; Index++)
        {
            if (Vector3Equals(StarsOf(&First)[Index].Direction, StarsOf(&Other)[Index].Direction))
            {
                SameDirections++;
            }
        }
        TEST_CHECK(run, SameDirections == 0U);
    }
    Memory_Free(First._data);
    Memory_Free(Again._data);
    Memory_Free(Other._data);
}

static void DenserCatalogueStartsWithTheSameStars(TestRun* run)
{
    GenericBuffer Sparse;
    GenericBuffer Dense;
    if (GenerateCatalogue(run, TEST_STAR_SEED, 0.25f, &Sparse) && GenerateCatalogue(run, TEST_STAR_SEED, 0.5f, &Dense))
    {
        TEST_CHECK(run, Sparse._count < Dense._count);
        TEST_CHECK(run, Memory_IsEqual(Sparse._data, Dense._data, Sparse._count * sizeof(NightSkyStar)));
    }
    Memory_Free(Sparse._data);
    Memory_Free(Dense._data);
}

static void StarCountFollowsTheDensity(TestRun* run)
{
    TEST_CHECK(run, NightSky_GetStarCount(1.0f) == 9000U);
    TEST_CHECK(run, NightSky_GetStarCount(0.5f) == 4500U);
    TEST_CHECK(run, NightSky_GetStarCount(0.0f) == 0U);
    TEST_CHECK(run, NightSky_GetStarCount(-1.0f) == 0U);
    TEST_CHECK(run, NightSky_GetStarCount(NAN) == 0U);
    TEST_CHECK(run, NightSky_GetStarCount(INFINITY) == 0U);
    TEST_CHECK(run, NightSky_GetStarCount(10.0f) == NIGHT_SKY_MAX_STAR_COUNT);

    // The stars are appended after what the buffer already holds.
    GenericBuffer Stars;
    GenericBuffer_AllocateVariable(&Stars, 0U, sizeof(NightSkyStar));
    NightSkyStar Existing = { 0 };
    GenericBuffer_AddLast(&Stars, &Existing);
    TEST_CHECK_SUCCESS(run, NightSky_GenerateStars(TEST_STAR_SEED, 0.1f, &Stars));
    TEST_CHECK(run, Stars._count == 901U);
    Memory_Free(Stars._data);

    GenericBuffer Bytes;
    GenericBuffer_AllocateVariable(&Bytes, 0U, sizeof(unsigned char));
    TEST_CHECK_ERROR(run, NightSky_GenerateStars(TEST_STAR_SEED, 1.0f, &Bytes), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, NightSky_GenerateStars(TEST_STAR_SEED, 1.0f, NULL), ErrorCode_IllegalArgument);
    Memory_Free(Bytes._data);
}

static void StarsHaveValidValues(TestRun* run)
{
    GenericBuffer Stars;
    if (GenerateCatalogue(run, TEST_STAR_SEED, 1.0f, &Stars))
    {
        size_t BadCount = 0U;
        for (size_t Index = 0U; Index < Stars._count; Index++)
        {
            const NightSkyStar* Star = &StarsOf(&Stars)[Index];
            float Brightest = fmaxf((float)Star->Color.r, fmaxf((float)Star->Color.g, (float)Star->Color.b));
            bool IsValid = (fabsf(Vector3Length(Star->Direction) - 1.0f) < 1e-5f)
                && (Star->Magnitude >= NIGHT_SKY_BRIGHTEST_MAGNITUDE) && (Star->Magnitude <= NIGHT_SKY_FAINTEST_MAGNITUDE)
                && isfinite(Star->Flux) && (Star->Flux > 0.0f)
                && (fabsf(Star->Flux - powf(10.0f, -0.4f * Star->Magnitude)) <= (1e-5f * Star->Flux))
                && (Star->ColorIndex >= -0.35f) && (Star->ColorIndex <= 2.0f)
                && (Brightest == 255.0f) && (Star->Color.a == 255)
                && (Star->TwinkleSeed >= 0.0f) && (Star->TwinkleSeed < 1.0f);
            if (!IsValid)
            {
                BadCount++;
            }
        }
        TEST_CHECK(run, Stars._count == 9000U);
        TEST_CHECK(run, BadCount == 0U);
    }
    Memory_Free(Stars._data);
}

static void FaintStarsOutnumberBrightOnes(TestRun* run)
{
    // Stars brighter than magnitude 4 should be about 1 / 2.8 of those brighter than 5.
    GenericBuffer Stars;
    if (GenerateCatalogue(run, TEST_STAR_SEED, 2.0f, &Stars))
    {
        size_t BrighterThan4 = 0U;
        size_t BrighterThan5 = 0U;
        for (size_t Index = 0U; Index < Stars._count; Index++)
        {
            float Magnitude = StarsOf(&Stars)[Index].Magnitude;
            BrighterThan4 += (Magnitude < 4.0f) ? 1U : 0U;
            BrighterThan5 += (Magnitude < 5.0f) ? 1U : 0U;
        }
        double Ratio = (double)BrighterThan4 / (double)BrighterThan5;
        TEST_CHECK(run, (Ratio > 0.30) && (Ratio < 0.42));
    }
    Memory_Free(Stars._data);
}

static void StarsCrowdTheGalacticPlane(TestRun* run)
{
    // Within 10 degrees of the galactic plane: 17% of the sky, about 26% of the stars with the crowding.
    GenericBuffer Stars;
    if (GenerateCatalogue(run, TEST_STAR_SEED, 2.0f, &Stars))
    {
        Matrix CelestialToGalactic = NightSky_GetCelestialToGalactic();
        size_t NearPlane = 0U;
        for (size_t Index = 0U; Index < Stars._count; Index++)
        {
            Vector3 Galactic = Vector3Transform(StarsOf(&Stars)[Index].Direction, CelestialToGalactic);
            NearPlane += (fabsf(Galactic.z) < sinf(10.0f * DEG2RAD)) ? 1U : 0U;
        }
        double Share = (double)NearPlane / (double)Stars._count;
        TEST_CHECK(run, (Share > 0.22) && (Share < 0.30));
    }
    Memory_Free(Stars._data);
}

static void ColorsAreMostlyWhiteToYellow(TestRun* run)
{
    GenericBuffer Stars;
    if (GenerateCatalogue(run, TEST_STAR_SEED, 1.0f, &Stars))
    {
        size_t Blue = 0U;
        size_t WhiteToYellow = 0U;
        size_t Orange = 0U;
        for (size_t Index = 0U; Index < Stars._count; Index++)
        {
            float ColorIndex = StarsOf(&Stars)[Index].ColorIndex;
            Blue += (ColorIndex < -0.05f) ? 1U : 0U;
            WhiteToYellow += ((ColorIndex >= -0.05f) && (ColorIndex <= 0.9f)) ? 1U : 0U;
            Orange += (ColorIndex > 1.0f) ? 1U : 0U;
        }
        double Count = (double)Stars._count;
        TEST_CHECK(run, ((double)WhiteToYellow / Count) > 0.5);
        TEST_CHECK(run, ((double)Blue / Count) > 0.05);
        TEST_CHECK(run, ((double)Orange / Count) > 0.05);
    }
    Memory_Free(Stars._data);

    // The sun (B-V 0.65) is about 5778 K, warm white; hot stars are blue-white, cool ones orange.
    TEST_CHECK(run, fabsf(NightSky_GetColorTemperature(0.65f) - 5778.0f) < 50.0f);
    TEST_CHECK(run, NightSky_GetColorTemperature(-0.3f) > NightSky_GetColorTemperature(1.5f));
    Color Sun = NightSky_GetTemperatureColor(5778.0f);
    Color Hot = NightSky_GetTemperatureColor(20000.0f);
    Color Cool = NightSky_GetTemperatureColor(3000.0f);
    TEST_CHECK(run, (Sun.r == 255) && (Sun.g > 230) && (Sun.b > 210) && (Sun.b < Sun.g));
    TEST_CHECK(run, (Hot.b == 255) && (Hot.r < 200));
    TEST_CHECK(run, (Cool.r == 255) && (Cool.b < 150));
}


// Tests: visibility.
static void LimitingMagnitudeFollowsPollutionAndTwilight(TestRun* run)
{
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(0.0f, 0.0f, -30.0f) - 6.5f) < 1e-4f);
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(1.0f, 0.0f, -30.0f) - 3.0f) < 1e-4f);
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(0.5f, 0.0f, -30.0f) - 4.75f) < 1e-4f);
    // Twilight: only the brightest stars at -6 degrees, none with the sun up.
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(0.0f, 0.0f, -6.0f) - 1.5f) < 1e-4f);
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(0.0f, 0.0f, -12.0f) - 4.5f) < 1e-4f);
    TEST_CHECK(run, NightSky_GetLimitingMagnitude(0.0f, 0.0f, 0.0f) < NIGHT_SKY_BRIGHTEST_MAGNITUDE);
    TEST_CHECK(run, NightSky_GetLimitingMagnitude(0.0f, 0.0f, 20.0f) < NIGHT_SKY_BRIGHTEST_MAGNITUDE);
    // The lower of the two limits wins.
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(1.0f, 0.0f, -12.0f) - 3.0f) < 1e-4f);
    // Bad inputs: pollution clamps, NaN pollution counts as none, a NaN sun counts as day.
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(2.0f, 0.0f, -30.0f) - 3.0f) < 1e-4f);
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(NAN, 0.0f, -30.0f) - 6.5f) < 1e-4f);
    TEST_CHECK(run, NightSky_GetLimitingMagnitude(0.0f, 0.0f, NAN) < NIGHT_SKY_BRIGHTEST_MAGNITUDE);
}

/* A high full moon hides the faint stars and most of the Milky Way; a quarter moon (a tenth of its light) still dims the
 * sky noticeably; a city sky has little left for the moon to take. */
static void MoonlightHidesFaintStars(TestRun* run)
{
    float FullMoon = NightSky_GetLimitingMagnitude(0.0f, 1.0f, -30.0f);
    TEST_CHECK(run, fabsf(FullMoon - (NIGHT_SKY_DARK_SITE_LIMITING_MAGNITUDE - NIGHT_SKY_FULL_MOON_MAGNITUDES)) < 1e-4f);
    TEST_CHECK(run, NightSky_GetMilkyWayVisibility(FullMoon) < 0.1f);
    float QuarterMoon = NightSky_GetLimitingMagnitude(0.0f, 0.09f, -30.0f);
    TEST_CHECK(run, (QuarterMoon < 6.0f) && (QuarterMoon > 5.8f));
    TEST_CHECK(run, NightSky_GetLimitingMagnitude(0.0f, 0.001f, -30.0f) > 6.45f);

    // Light pollution and the moon both brighten the sky, but the moon takes less from an already bright one.
    float SmallTown = NightSky_GetLimitingMagnitude(0.35f, 1.0f, -30.0f);
    TEST_CHECK(run, (SmallTown < NightSky_GetLimitingMagnitude(0.35f, 0.0f, -30.0f)) && (SmallTown < FullMoon));
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(1.0f, 1.0f, -30.0f) - 3.0f) < 1e-4f);

    // Brighter moonlight takes more, up to a limit; bad values count as no moon; twilight still wins by day.
    TEST_CHECK(run, NightSky_GetLimitingMagnitude(0.0f, 4.0f, -30.0f) < FullMoon);
    TEST_CHECK(run, NightSky_GetLimitingMagnitude(0.0f, 1000.0f, -30.0f)
        == NightSky_GetLimitingMagnitude(0.0f, NIGHT_SKY_MAX_MOONLIT_SKY, -30.0f));
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(0.0f, NAN, -30.0f) - 6.5f) < 1e-4f);
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(0.0f, -1.0f, -30.0f) - 6.5f) < 1e-4f);
    TEST_CHECK(run, fabsf(NightSky_GetLimitingMagnitude(0.0f, 1.0f, -6.0f) - 1.5f) < 1e-4f);
}

static void MilkyWayFadesWithTheLimit(TestRun* run)
{
    TEST_CHECK(run, NightSky_GetMilkyWayVisibility(3.0f) == 0.0f);
    TEST_CHECK(run, NightSky_GetMilkyWayVisibility(4.5f) == 0.0f);
    TEST_CHECK(run, fabsf(NightSky_GetMilkyWayVisibility(5.25f) - 0.5f) < 1e-5f);
    TEST_CHECK(run, NightSky_GetMilkyWayVisibility(6.0f) == 1.0f);
    TEST_CHECK(run, NightSky_GetMilkyWayVisibility(6.5f) == 1.0f);
}

static void AirmassGrowsTowardTheHorizon(TestRun* run)
{
    TEST_CHECK(run, fabsf(NightSky_GetAirmass(1.0f) - 1.0f) < 1e-3f);
    TEST_CHECK(run, fabsf(NightSky_GetAirmass(0.5f) - 1.994f) < 0.01f);  // 30 degrees altitude
    TEST_CHECK(run, fabsf(NightSky_GetAirmass(0.0f) - 37.92f) < 0.1f);   // the horizon
    TEST_CHECK(run, NightSky_GetAirmass(-0.5f) == NightSky_GetAirmass(0.0f));
    bool IsMonotonic = true;
    float Previous = NightSky_GetAirmass(0.0f);
    for (int Step = 1; Step <= 100; Step++)
    {
        float Current = NightSky_GetAirmass((float)Step / 100.0f);
        IsMonotonic = IsMonotonic && (Current < Previous);
        Previous = Current;
    }
    TEST_CHECK(run, IsMonotonic);
}


// Tests: the galaxy among the stars.
static void GalaxySitsWhereItDoesOnEarth(TestRun* run)
{
    Matrix CelestialToGalactic = NightSky_GetCelestialToGalactic();
    Matrix GalacticToCelestial = MatrixTranspose(CelestialToGalactic);
    Vector3 NorthPole = { 0.0f, 0.0f, -1.0f }; // the celestial pole above the horizon

    Vector3 Centre = Vector3Transform((Vector3){ 1.0f, 0.0f, 0.0f }, GalacticToCelestial);
    Vector3 GalacticPole = Vector3Transform((Vector3){ 0.0f, 0.0f, 1.0f }, GalacticToCelestial);
    double CentreDeclination = asin((double)Vector3DotProduct(Centre, NorthPole)) * TEST_DEGREES;
    double PlaneTilt = acos((double)Vector3DotProduct(GalacticPole, NorthPole)) * TEST_DEGREES;
    TEST_CHECK(run, fabs(CentreDeclination - (-28.94)) < 0.1);
    TEST_CHECK(run, fabs(PlaneTilt - 62.87) < 0.1);
    TEST_CHECK(run, fabsf(Vector3Length(Centre) - 1.0f) < 1e-5f);
    TEST_CHECK(run, fabsf(Vector3DotProduct(Centre, GalacticPole)) < 1e-5f);

    // The seasons of the sky, at midnight (+X is east, +Z the side where the sun passes at noon):
    // - a June night: the galactic centre stands high in the south;
    // - late September: it has set in the west by midnight (in March it would be about to rise in the east instead,
    //   so this also checks which way the stars drift through the year);
    // - December: it stays below the horizon all night, near the sun.
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.TimeOfDay = 0.0f;
    Environment.SunAngle = 0.4f;
    TEST_CHECK(run, GameCalendar_TryGetDayNumber((GameDate){ .Year = 2026, .Month = 6, .Day = 21 }, &Environment.Date));
    Vector3 June = Vector3Transform(Centre, WorldEnvironment_GetStarsToWorld(&Environment));
    TEST_CHECK(run, (June.y > 0.5f) && (June.z > 0.5f));
    TEST_CHECK(run, GameCalendar_TryGetDayNumber((GameDate){ .Year = 2026, .Month = 9, .Day = 23 }, &Environment.Date));
    Vector3 September = Vector3Transform(Centre, WorldEnvironment_GetStarsToWorld(&Environment));
    TEST_CHECK(run, (September.y < 0.0f) && (September.x < -0.5f));
    TEST_CHECK(run, GameCalendar_TryGetDayNumber((GameDate){ .Year = 2026, .Month = 12, .Day = 21 }, &Environment.Date));
    Vector3 December = Vector3Transform(Centre, WorldEnvironment_GetStarsToWorld(&Environment));
    TEST_CHECK(run, December.y < -0.5f);
    WorldEnvironment_Deconstruct(&Environment);
}


// Public functions.
void NightSkyTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "catalogue_is_deterministic_per_seed", CatalogueIsDeterministicPerSeed },
        { "denser_catalogue_starts_with_the_same_stars", DenserCatalogueStartsWithTheSameStars },
        { "star_count_follows_the_density", StarCountFollowsTheDensity },
        { "stars_have_valid_values", StarsHaveValidValues },
        { "faint_stars_outnumber_bright_ones", FaintStarsOutnumberBrightOnes },
        { "stars_crowd_the_galactic_plane", StarsCrowdTheGalacticPlane },
        { "colors_are_mostly_white_to_yellow", ColorsAreMostlyWhiteToYellow },
        { "limiting_magnitude_follows_pollution_and_twilight", LimitingMagnitudeFollowsPollutionAndTwilight },
        { "moonlight_hides_faint_stars", MoonlightHidesFaintStars },
        { "milky_way_fades_with_the_limit", MilkyWayFadesWithTheLimit },
        { "airmass_grows_toward_the_horizon", AirmassGrowsTowardTheHorizon },
        { "galaxy_sits_where_it_does_on_earth", GalaxySitsWhereItDoesOnEarth },
    };
    TestRun_RunSuite(run, "NightSky", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
