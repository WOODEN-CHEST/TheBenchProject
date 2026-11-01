#include "TestSuites.h"
#include "CloudNoise.h"
#include "wr/WRMemory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


// Macros.
/** How many points the noises are checked at. */
#define NOISE_POINT_COUNT 400
/** A histogram's bins for the textures' spread, and how far each may stray from an even share. */
#define SPREAD_BIN_COUNT 8
#define SPREAD_TOLERANCE 0.01
/** The textures' fingerprints (FNV-1a, 64 bits) at CLOUD_NOISE_VERSION 1: the clouds' look was tuned on these bytes. A
 *  change to the noise bumps the version and these with it. */
#define SHAPE_FINGERPRINT 0x998B97DC0C0258B5ULL
#define DETAIL_FINGERPRINT 0x2CD737089FEF7085ULL
#define WEATHER_FINGERPRINT 0x1021E03107D6BA8FULL


// Static functions.
/* A repeatable pseudo-random value in [0, 1) (a linear congruential sequence). */
static double NextUnit(uint64_t* state)
{
    *state = (*state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return (double)(*state >> 11) / 9007199254740992.0;
}

/* The three textures, filled. */
typedef struct NoiseTexturesStruct
{
    uint8_t* Shape;
    uint8_t* Detail;
    uint8_t* Weather;
} NoiseTextures;

static size_t ShapeBytes(void)
{
    return (size_t)CLOUD_NOISE_SHAPE_SIZE * CLOUD_NOISE_SHAPE_SIZE * CLOUD_NOISE_SHAPE_SIZE;
}

static size_t DetailBytes(void)
{
    return (size_t)CLOUD_NOISE_DETAIL_SIZE * CLOUD_NOISE_DETAIL_SIZE * CLOUD_NOISE_DETAIL_SIZE;
}

static size_t WeatherBytes(void)
{
    return (size_t)CLOUD_NOISE_WEATHER_SIZE * CLOUD_NOISE_WEATHER_SIZE * 4U;
}

static void FillTextures(NoiseTextures* outTextures)
{
    outTextures->Shape = Memory_Allocate(ShapeBytes());
    outTextures->Detail = Memory_Allocate(DetailBytes());
    outTextures->Weather = Memory_Allocate(WeatherBytes());
    CloudNoise_FillShape(outTextures->Shape);
    CloudNoise_FillDetail(outTextures->Detail);
    CloudNoise_FillWeather(outTextures->Weather);
}

static void ReleaseTextures(NoiseTextures* textures)
{
    Memory_Free(textures->Shape);
    Memory_Free(textures->Detail);
    Memory_Free(textures->Weather);
}

/* Whether every byte (every `stride`-th from `first`) falls into each of SPREAD_BIN_COUNT even bins about as often. */
static bool IsSpreadEvenly(const uint8_t* bytes, size_t count, size_t first, size_t stride)
{
    size_t Bins[SPREAD_BIN_COUNT] = { 0 };
    size_t Total = 0U;
    for (size_t Index = first; Index < count; Index += stride)
    {
        Bins[bytes[Index] * SPREAD_BIN_COUNT / 256]++;
        Total++;
    }
    for (int Bin = 0; Bin < SPREAD_BIN_COUNT; Bin++)
    {
        double Share = (double)Bins[Bin] / (double)Total;
        if (fabs(Share - 1.0 / SPREAD_BIN_COUNT) > SPREAD_TOLERANCE)
        {
            return false;
        }
    }
    return true;
}

/* A byte string's FNV-1a hash (64 bits). */
static uint64_t Fingerprint(const uint8_t* bytes, size_t count)
{
    uint64_t Hash = 14695981039346656037ULL;
    for (size_t Index = 0U; Index < count; Index++)
    {
        Hash ^= bytes[Index];
        Hash *= 1099511628211ULL;
    }
    return Hash;
}

/* The mean difference between neighbours along x across a texture's seam (the last texel and the first of each row)
 * over the mean difference between neighbours inside it: about 1 when the texture repeats without a seam. */
static double SeamRatio(const uint8_t* texels, int32_t width, int32_t rows, size_t stride, size_t channel)
{
    double Inside = 0.0;
    double Seam = 0.0;
    for (int32_t Row = 0; Row < rows; Row++)
    {
        const uint8_t* Line = &texels[(size_t)Row * (size_t)width * stride];
        for (int32_t X = 0; X + 1 < width; X++)
        {
            Inside += fabs((double)Line[(size_t)X * stride + channel] - (double)Line[(size_t)(X + 1) * stride + channel]);
        }
        Seam += fabs((double)Line[(size_t)(width - 1) * stride + channel] - (double)Line[channel]);
    }
    Inside /= (double)rows * (double)(width - 1);
    Seam /= (double)rows;
    return Seam / Inside;
}


// Static functions: tests.
static void GradientNoiseRepeatsAndStaysInRange(TestRun* run)
{
    uint64_t State = 7U;
    bool AreRepeating = true;
    bool AreInRange = true;
    for (int Index = 0; Index < NOISE_POINT_COUNT; Index++)
    {
        int32_t Period = 1 + (int32_t)(NextUnit(&State) * 12.0);
        double X = NextUnit(&State) * 40.0 - 20.0;
        double Y = NextUnit(&State) * 40.0 - 20.0;
        double Z = NextUnit(&State) * 40.0 - 20.0;
        double Value = CloudNoise_Gradient3(X, Y, Z, Period, 3U);
        AreInRange = AreInRange && (Value >= -1.0) && (Value <= 1.0);
        AreRepeating = AreRepeating && (fabs(Value - CloudNoise_Gradient3(X + Period, Y, Z, Period, 3U)) < 1e-9)
            && (fabs(Value - CloudNoise_Gradient3(X, Y - Period, Z, Period, 3U)) < 1e-9)
            && (fabs(Value - CloudNoise_Gradient3(X, Y, Z + 2.0 * Period, Period, 3U)) < 1e-9);
        double Flat = CloudNoise_Gradient2(X, Y, Period, 3U);
        AreInRange = AreInRange && (Flat >= -1.0) && (Flat <= 1.0);
        AreRepeating = AreRepeating && (fabs(Flat - CloudNoise_Gradient2(X - Period, Y + Period, Period, 3U)) < 1e-9);
    }
    TEST_CHECK(run, AreRepeating && AreInRange);
    // Nothing at the lattice's points; something between them; another seed, other hills.
    TEST_CHECK(run, (CloudNoise_Gradient3(3.0, -2.0, 5.0, 8, 3U) == 0.0) && (CloudNoise_Gradient2(4.0, 1.0, 8, 3U) == 0.0));
    TEST_CHECK(run, CloudNoise_Gradient3(3.4, -2.3, 5.7, 8, 3U) != CloudNoise_Gradient3(3.4, -2.3, 5.7, 8, 4U));
    double Largest = 0.0;
    for (int Index = 0; Index < NOISE_POINT_COUNT; Index++)
    {
        Largest = fmax(Largest, fabs(CloudNoise_Gradient3(Index * 0.173, Index * 0.311, Index * 0.057, 16, 9U)));
    }
    TEST_CHECK(run, Largest > 0.3); // not flat
}

static void WorleyNoiseRepeatsAndFindsTheNearestPoint(TestRun* run)
{
    uint64_t State = 11U;
    bool AreRepeating = true;
    bool AreNearest = true;
    bool AreInRange = true;
    for (int Index = 0; Index < NOISE_POINT_COUNT; Index++)
    {
        int32_t Period = 3 + (int32_t)(NextUnit(&State) * 6.0);
        double X = NextUnit(&State) * 20.0 - 10.0;
        double Y = NextUnit(&State) * 20.0 - 10.0;
        double Z = NextUnit(&State) * 20.0 - 10.0;
        double Value = CloudNoise_Worley3(X, Y, Z, Period, 5U);
        AreInRange = AreInRange && (Value >= 0.0) && (Value <= 1.0);
        AreRepeating = AreRepeating && (fabs(Value - CloudNoise_Worley3(X + Period, Y, Z - Period, Period, 5U)) < 1e-9);
        // No point the search missed lies nearer: the nearest point to any probe P is at most |X - P| + noise(P) from X
        // (the triangle inequality), so noise(X) is at most that for probes all over the 5 x 5 x 5 cells around X; a
        // search over too few cells would miss a point some probe finds.
        double Best = Value;
        for (int OffsetZ = -2; OffsetZ <= 2; OffsetZ++)
        {
            for (int OffsetY = -2; OffsetY <= 2; OffsetY++)
            {
                for (int OffsetX = -2; OffsetX <= 2; OffsetX++)
                {
                    for (int Probe = 0; Probe < 8; Probe++)
                    {
                        double PX = floor(X) + OffsetX + ((Probe & 1) ? 0.75 : 0.25);
                        double PY = floor(Y) + OffsetY + ((Probe & 2) ? 0.75 : 0.25);
                        double PZ = floor(Z) + OffsetZ + ((Probe & 4) ? 0.75 : 0.25);
                        double Gap = sqrt((PX - X) * (PX - X) + (PY - Y) * (PY - Y) + (PZ - Z) * (PZ - Z));
                        Best = fmin(Best, Gap + CloudNoise_Worley3(PX, PY, PZ, Period, 5U));
                    }
                }
            }
        }
        AreNearest = AreNearest && (Value <= Best + 1e-12);
    }
    TEST_CHECK(run, AreRepeating && AreNearest && AreInRange);
    TEST_CHECK(run, fabs(CloudNoise_Worley2(1.3, 2.9, 4, 5U) - CloudNoise_Worley2(5.3, -1.1, 4, 5U)) < 1e-9);
    TEST_CHECK(run, CloudNoise_Worley2(1.3, 2.9, 4, 5U) != CloudNoise_Worley2(1.3, 2.9, 4, 6U));
}

static void TheTexturesAreTheSameEveryTime(TestRun* run)
{
    NoiseTextures First;
    NoiseTextures Second;
    FillTextures(&First);
    // Filled in another order: no state carries from one fill to the next.
    Second.Shape = Memory_Allocate(ShapeBytes());
    Second.Detail = Memory_Allocate(DetailBytes());
    Second.Weather = Memory_Allocate(WeatherBytes());
    CloudNoise_FillWeather(Second.Weather);
    CloudNoise_FillDetail(Second.Detail);
    CloudNoise_FillShape(Second.Shape);
    TEST_CHECK(run, memcmp(First.Shape, Second.Shape, ShapeBytes()) == 0);
    TEST_CHECK(run, memcmp(First.Detail, Second.Detail, DetailBytes()) == 0);
    TEST_CHECK(run, memcmp(First.Weather, Second.Weather, WeatherBytes()) == 0);
    ReleaseTextures(&First);
    ReleaseTextures(&Second);
}

static void TheTexturesMatchTheirVersion(TestRun* run)
{
    NoiseTextures Textures;
    FillTextures(&Textures);
    TEST_CHECK(run, CLOUD_NOISE_VERSION == 1);
    uint64_t Shape = Fingerprint(Textures.Shape, ShapeBytes());
    uint64_t Detail = Fingerprint(Textures.Detail, DetailBytes());
    uint64_t Weather = Fingerprint(Textures.Weather, WeatherBytes());
    if (!TEST_CHECK(run, (Shape == SHAPE_FINGERPRINT) && (Detail == DETAIL_FINGERPRINT)
        && (Weather == WEATHER_FINGERPRINT)))
    {
        printf("    the textures' fingerprints: 0x%016llX 0x%016llX 0x%016llX\n", (unsigned long long)Shape,
            (unsigned long long)Detail, (unsigned long long)Weather);
    }
    ReleaseTextures(&Textures);
}

static void TheTexturesSpreadEvenly(TestRun* run)
{
    NoiseTextures Textures;
    FillTextures(&Textures);
    TEST_CHECK(run, IsSpreadEvenly(Textures.Shape, ShapeBytes(), 0U, 1U));
    TEST_CHECK(run, IsSpreadEvenly(Textures.Detail, DetailBytes(), 0U, 1U));
    for (size_t Channel = 0U; Channel < 4U; Channel++)
    {
        if (!TEST_CHECK(run, IsSpreadEvenly(Textures.Weather, WeatherBytes(), Channel, 4U)))
        {
            printf("    in the weather map's channel %zu\n", Channel);
        }
    }
    ReleaseTextures(&Textures);
}

static void TheTexturesRepeatWithoutASeam(TestRun* run)
{
    NoiseTextures Textures;
    FillTextures(&Textures);
    // Along x, the rows of every slice (the volumes) or the map's rows; each channel of the map.
    double Shape = SeamRatio(Textures.Shape, CLOUD_NOISE_SHAPE_SIZE, CLOUD_NOISE_SHAPE_SIZE * CLOUD_NOISE_SHAPE_SIZE, 1U,
        0U);
    double Detail = SeamRatio(Textures.Detail, CLOUD_NOISE_DETAIL_SIZE, CLOUD_NOISE_DETAIL_SIZE * CLOUD_NOISE_DETAIL_SIZE,
        1U, 0U);
    TEST_CHECK(run, (Shape > 0.5) && (Shape < 1.5) && (Detail > 0.5) && (Detail < 1.5));
    for (size_t Channel = 0U; Channel < 4U; Channel++)
    {
        double Ratio = SeamRatio(Textures.Weather, CLOUD_NOISE_WEATHER_SIZE, CLOUD_NOISE_WEATHER_SIZE, 4U, Channel);
        if (!TEST_CHECK(run, (Ratio > 0.5) && (Ratio < 1.5)))
        {
            printf("    the weather map's channel %zu: %f\n", Channel, Ratio);
        }
    }
    // Along z through the shape volume: its last slice meets its first.
    double Inside = 0.0;
    double Seam = 0.0;
    size_t Slice = (size_t)CLOUD_NOISE_SHAPE_SIZE * CLOUD_NOISE_SHAPE_SIZE;
    for (size_t Texel = 0U; Texel < Slice; Texel++)
    {
        Inside += fabs((double)Textures.Shape[Texel] - (double)Textures.Shape[Slice + Texel]);
        Seam += fabs((double)Textures.Shape[(CLOUD_NOISE_SHAPE_SIZE - 1) * Slice + Texel] - (double)Textures.Shape[Texel]);
    }
    TEST_CHECK(run, (Seam / Inside > 0.5) && (Seam / Inside < 1.5));
    ReleaseTextures(&Textures);
}


// Public functions.
void CloudNoiseTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "gradient_noise_repeats_and_stays_in_range", GradientNoiseRepeatsAndStaysInRange },
        { "worley_noise_repeats_and_finds_the_nearest_point", WorleyNoiseRepeatsAndFindsTheNearestPoint },
        { "the_textures_are_the_same_every_time", TheTexturesAreTheSameEveryTime },
        { "the_textures_match_their_version", TheTexturesMatchTheirVersion },
        { "the_textures_spread_evenly", TheTexturesSpreadEvenly },
        { "the_textures_repeat_without_a_seam", TheTexturesRepeatWithoutASeam },
    };
    TestRun_RunSuite(run, "CloudNoise", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
