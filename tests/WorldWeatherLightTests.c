#include "TestSuites.h"
#include "WorldWeatherLight.h"
#include "WorldAtmosphere.h"
#include <math.h>
#include <string.h>


// Macros.
/** How close two lights must agree, relative. */
#define TEST_TOLERANCE 1.0e-5f


// Static functions.
static bool IsClose(float a, float b)
{
    return fabsf(a - b) <= TEST_TOLERANCE * fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));
}

/* A clear frame's light at noon: the sun's light on a surface facing it, the sky's irradiance, the ground's bounce. */
static WorldSkyLightFrame MakeClearLight(void)
{
    WorldSkyLightFrame Light;
    memset(&Light, 0, sizeof(Light));
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Light.SunAboveAir[Channel] = 1.078f;
        Light.Sun[Channel] = 0.8f - 0.05f * (float)Channel;
        Light.Sky[Channel] = 0.2f + 0.02f * (float)Channel;
        Light.SunSky[Channel] = Light.Sky[Channel];
        Light.Ground[Channel] = 0.09f;
        Light.AmbientTint[Channel] = 1.0f;
        Light.Night[Channel] = 0.004f;
    }
    return Light;
}

static WorldEnvironment MakeEnvironment(float coverage, float density)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    Environment.CloudCoverage = coverage;
    Environment.CloudDensity = density;
    return Environment;
}


// Static functions: tests.
static void NoWeatherLeavesTheLightAlone(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment(0.0f, 0.0f);
    Environment.CloudCoverage = 1.0f; // coverage of clouds with no thickness is no cloud
    WorldWeatherLight Weather;
    WorldWeatherLight_GetFrame(&Environment, &Weather);
    TEST_CHECK(run, (Weather.CloudCover == 0.0f) && (Weather.DirectShare == 1.0f) && (Weather.CloudTransmission == 1.0f));
    TEST_CHECK(run, !WorldWeatherLight_HasClouds(&Weather) && !WorldWeatherLight_IsVisible(&Weather));
    WorldSkyLightFrame Clear = MakeClearLight();
    WorldSkyLightFrame Frame = Clear;
    float Albedo[3] = { 0.1f, 0.1f, 0.1f };
    WorldWeatherLight_AddDeck(&Weather, &Clear, (Vector3){ 0.0f, 0.9f, 0.43f }, (Vector3){ 0.0f, -1.0f, 0.0f }, Albedo,
        &Frame);
    TEST_CHECK(run, memcmp(&Frame, &Clear, sizeof(Frame)) == 0); // bit for bit
    for (int Channel = 0; Channel < 3; Channel++)
    {
        TEST_CHECK(run, (Weather.DeckIrradiance[Channel] == 0.0f) && (Weather.ZenithRadiance[Channel] == 0.0f)
            && (Weather.AmbientRadiance[Channel] == 0.0f) && (Weather.FlashIrradiance[Channel] == 0.0f));
    }
    WorldEnvironment_Deconstruct(&Environment);
}

static void CloudsCoverAndTransmitByTheirDepth(TestRun* run)
{
    float Previous = 2.0f;
    bool IsFalling = true;
    for (int Step = 0; Step <= 20; Step++)
    {
        float Density = (float)Step / 20.0f;
        WorldEnvironment Environment = MakeEnvironment(1.0f, Density);
        WorldWeatherLight Weather;
        WorldWeatherLight_GetFrame(&Environment, &Weather);
        IsFalling = IsFalling && (Weather.CloudTransmission < Previous) && (Weather.CloudTransmission > 0.0f);
        Previous = Weather.CloudTransmission;
        TEST_CHECK(run, IsClose(Weather.DirectShare, 1.0f - Weather.CloudCover));
        WorldEnvironment_Deconstruct(&Environment);
    }
    TEST_CHECK(run, IsFalling);
    // A storm cloud (depth 64) passes about an eighth (two-stream: 1 / (1 + 0.75 x 0.15 x 64)) and hides the sun.
    WorldEnvironment Storm = MakeEnvironment(1.0f, 1.0f);
    WorldWeatherLight Weather;
    WorldWeatherLight_GetFrame(&Storm, &Weather);
    TEST_CHECK(run, IsClose(Weather.CloudTransmission, 1.0f / (1.0f + 0.1125f * 64.0f)));
    TEST_CHECK(run, (Weather.CloudCover == 1.0f) && (Weather.DirectShare == 0.0f));
    // Half the sky covered by thick clouds: half the sun.
    Storm.CloudCoverage = 0.5f;
    Storm.CloudDensity = 0.5f;
    WorldWeatherLight_GetFrame(&Storm, &Weather);
    TEST_CHECK(run, IsClose(Weather.CloudCover, 0.5f) && IsClose(Weather.DirectShare, 0.5f));
    WorldEnvironment_Deconstruct(&Storm);
}

static void TheDeckSendsDownWhatTheCloudsPass(TestRun* run)
{
    Vector3 Sun = { 0.0f, 0.8f, 0.6f };
    Vector3 Moon = { 0.0f, -0.5f, 0.866f };
    float Albedo[3] = { 0.1f, 0.12f, 0.14f };
    WorldSkyLightFrame Clear = MakeClearLight();
    bool IsConserved = true;
    for (int CoverStep = 0; CoverStep <= 10; CoverStep++)
    {
        for (int DensityStep = 1; DensityStep <= 10; DensityStep++)
        {
            WorldEnvironment Environment = MakeEnvironment((float)CoverStep / 10.0f, (float)DensityStep / 10.0f);
            WorldWeatherLight Weather;
            WorldWeatherLight_GetFrame(&Environment, &Weather);
            // The lights above the air dimmed by the clouds dim everything they light alike (as the renderer's second
            // frame light would give it).
            WorldSkyLightFrame Frame = Clear;
            for (int Channel = 0; Channel < 3; Channel++)
            {
                Frame.Sun[Channel] *= Weather.DirectShare;
                Frame.Sky[Channel] *= Weather.DirectShare;
                Frame.SunSky[Channel] *= Weather.DirectShare;
                Frame.Ground[Channel] *= Weather.DirectShare;
            }
            WorldWeatherLight_AddDeck(&Weather, &Clear, Sun, Moon, Albedo, &Frame);
            for (int Channel = 0; Channel < 3; Channel++)
            {
                float OnTop = Clear.Sun[Channel] * Sun.y + Clear.Sky[Channel];
                float Deck = Weather.CloudCover * Weather.CloudTransmission * OnTop;
                TEST_CHECK(run, IsClose(Weather.DeckIrradiance[Channel], Deck));
                TEST_CHECK(run, IsClose(Frame.Sky[Channel], Clear.Sky[Channel] * Weather.DirectShare + Deck));
                TEST_CHECK(run, IsClose(Frame.Ground[Channel], Clear.Ground[Channel] * Weather.DirectShare
                    + Albedo[Channel] * Deck));
                // The sun's part of the deck's light is daylight: the clear sky's SunSky is all of its sky here.
                TEST_CHECK(run, IsClose(Frame.SunSky[Channel], Clear.SunSky[Channel] * Weather.DirectShare + Deck));
                // The overcast sky's zenith for its irradiance (CIE: 9 / (7 pi)), and the air lit from all around.
                TEST_CHECK(run, IsClose(Weather.ZenithRadiance[Channel], Deck * 9.0f / (7.0f * 3.14159265f)));
                float Ambient = 0.5f * (Deck * 9.0f / (7.0f * 3.14159265f) * (2.0f / 3.0f)
                    + Albedo[Channel] * Deck / 3.14159265f);
                TEST_CHECK(run, IsClose(Weather.AmbientRadiance[Channel], Ambient));
                // Below the clouds a level surface never takes more than fell on their top.
                float Below = Frame.Sun[Channel] * Sun.y + Frame.Sky[Channel];
                IsConserved = IsConserved && (Below <= OnTop * (1.0f + TEST_TOLERANCE));
            }
            WorldEnvironment_Deconstruct(&Environment);
        }
    }
    TEST_CHECK(run, IsConserved);
}

static void AnOvercastDayIsDaylight(TestRun* run)
{
    float Albedo[3] = { 0.1f, 0.1f, 0.1f };
    WorldEnvironment Environment = MakeEnvironment(1.0f, 1.0f);
    WorldWeatherLight Weather;
    WorldWeatherLight_GetFrame(&Environment, &Weather);
    // Noon under a storm's deck: no sun comes through, and the deck's light is the day's.
    WorldSkyLightFrame Clear = MakeClearLight();
    WorldSkyLightFrame Frame = Clear;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Frame.Sun[Channel] = 0.0f;
        Frame.Sky[Channel] = 0.0f;
        Frame.SunSky[Channel] = 0.0f;
        Frame.Ground[Channel] = 0.0f;
    }
    WorldWeatherLight_AddDeck(&Weather, &Clear, (Vector3){ 0.0f, 0.9f, 0.43f }, (Vector3){ 0.0f, -1.0f, 0.0f }, Albedo,
        &Frame);
    float Day = WorldSkyLight_GetDayLuminance(&Frame);
    float Scene = WorldSkyLight_GetSceneLuminance(&Frame);
    TEST_CHECK(run, (Day > 0.0f) && IsClose(Day, Scene - 0.004f)); // all but the moonless night's own light
    // A moonlit night under the same deck: the deck's light is the moon's, none of it daylight.
    WorldSkyLightFrame Night;
    memset(&Night, 0, sizeof(Night));
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Night.Moon[Channel] = 0.02f;
        Night.Sky[Channel] = 0.003f;
        Night.AmbientTint[Channel] = 1.0f;
    }
    WorldSkyLightFrame Below = Night;
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Below.Moon[Channel] = 0.0f;
        Below.Sky[Channel] = 0.0f;
    }
    WorldWeatherLight_AddDeck(&Weather, &Night, (Vector3){ 0.0f, -0.9f, 0.43f }, (Vector3){ 0.0f, 0.8f, 0.6f }, Albedo,
        &Below);
    TEST_CHECK(run, (WorldSkyLight_GetSceneLuminance(&Below) > 0.0f) && (WorldSkyLight_GetDayLuminance(&Below) == 0.0f));
    WorldEnvironment_Deconstruct(&Environment);
}

static void TheFlashFallsOffWithDistance(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment(0.0f, 0.0f);
    Environment.LightningFlash = 1.0f;
    Environment.LightningDirection = (Vector3){ 1.0f, 0.0f, 0.0f };
    Environment.LightningDistance = 1000.0f;
    WorldWeatherLight Weather;
    WorldWeatherLight_GetFrame(&Environment, &Weather);
    float AtOne = Weather.FlashIrradiance[2];
    TEST_CHECK(run, IsClose(AtOne, WORLD_WEATHER_FLASH_IRRADIANCE));
    TEST_CHECK(run, (Weather.FlashIrradiance[0] < Weather.FlashIrradiance[1]) && (Weather.FlashIrradiance[1] < AtOne));
    TEST_CHECK(run, WorldWeatherLight_IsVisible(&Weather) && !WorldWeatherLight_HasClouds(&Weather));
    // Toward the clouds over the strike: its level direction, up to the clouds' height at its distance.
    TEST_CHECK(run, IsClose(Weather.FlashDirection.y / Weather.FlashDirection.x, WORLD_WEATHER_CLOUD_HEIGHT / 1000.0f)
        && (Weather.FlashDirection.z == 0.0f));
    Environment.LightningDistance = 2000.0f;
    WorldWeatherLight_GetFrame(&Environment, &Weather);
    TEST_CHECK(run, IsClose(Weather.FlashIrradiance[2], 0.5f * AtOne));
    Environment.LightningDistance = 10.0f; // a strike next door: at its most
    WorldWeatherLight_GetFrame(&Environment, &Weather);
    TEST_CHECK(run, IsClose(Weather.FlashIrradiance[2], WORLD_WEATHER_FLASH_MAX_IRRADIANCE));
    Environment.LightningFlash = 0.5f;
    Environment.LightningDistance = 1000.0f;
    WorldWeatherLight_GetFrame(&Environment, &Weather);
    TEST_CHECK(run, IsClose(Weather.FlashIrradiance[2], 0.5f * AtOne));
    // The flash lights the air too, with or without clouds.
    WorldSkyLightFrame Clear = MakeClearLight();
    WorldSkyLightFrame Frame = Clear;
    float Albedo[3] = { 0.1f, 0.1f, 0.1f };
    WorldWeatherLight_AddDeck(&Weather, &Clear, (Vector3){ 0.0f, 1.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, Albedo,
        &Frame);
    TEST_CHECK(run, (Weather.AmbientRadiance[2] > 0.0f) && (memcmp(&Frame, &Clear, sizeof(Frame)) == 0));
    WorldEnvironment_Deconstruct(&Environment);
}

static void BrokenValuesCountAsNone(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment(NAN, INFINITY);
    Environment.LightningFlash = NAN;
    Environment.LightningDistance = NAN;
    Environment.LightningDirection = (Vector3){ NAN, 0.0f, 0.0f };
    Environment.Wetness = INFINITY;
    Environment.WindDirection = NAN;
    Environment.WindSpeed = -3.0f;
    WorldWeatherLight Weather;
    WorldWeatherLight_GetFrame(&Environment, &Weather);
    TEST_CHECK(run, (Weather.CloudCover == 0.0f) && (Weather.DirectShare == 1.0f));
    TEST_CHECK(run, (Weather.FlashIrradiance[0] == 0.0f) && isfinite(Weather.FlashDirection.x)
        && isfinite(Weather.FlashDirection.y));
    TEST_CHECK(run, (Weather.Wetness == 0.0f) && (Weather.WindSpeed == 0.0f) && (Weather.WindDirection.z == 1.0f));
    WorldEnvironment_Deconstruct(&Environment);
}

static void TheWindPointsWhereItBlows(TestRun* run)
{
    WorldEnvironment Environment = MakeEnvironment(0.0f, 0.0f);
    Environment.WindDirection = 1.5707963f;
    Environment.WindSpeed = 7.0f;
    Environment.WindGust = 2.0f;
    Environment.Wetness = 0.25f;
    WorldWeatherLight Weather;
    WorldWeatherLight_GetFrame(&Environment, &Weather);
    TEST_CHECK(run, IsClose(Weather.WindDirection.x, 1.0f) && (fabsf(Weather.WindDirection.z) < 1.0e-6f));
    TEST_CHECK(run, (Weather.WindSpeed == 7.0f) && (Weather.WindGust == 2.0f) && (Weather.Wetness == 0.25f));
    TEST_CHECK(run, WorldWeatherLight_IsVisible(&Weather)); // wet surfaces show
    WorldEnvironment_Deconstruct(&Environment);
}

static void TheWeathersFogJoinsTheGroundHaze(TestRun* run)
{
    WorldEnvironment Environment;
    WorldEnvironment_SetDefaults(&Environment);
    WorldAtmosphereHaze Without;
    WorldAtmosphere_GetHaze(&Environment, 1.0f, &Without);
    Environment.FogDensity = 0.0f;
    Environment.FogHeight = 5.0f; // a height without a fog changes nothing
    WorldAtmosphereHaze NoFog;
    WorldAtmosphere_GetHaze(&Environment, 1.0f, &NoFog);
    TEST_CHECK(run, memcmp(&Without, &NoFog, sizeof(NoFog)) == 0);

    // The fog alone is exactly itself.
    Environment.FogDensity = 0.02f;
    Environment.FogHeight = 15.0f;
    WorldAtmosphereHaze Fog;
    WorldAtmosphere_GetHaze(&Environment, 1.0f, &Fog);
    TEST_CHECK(run, (Fog.GroundHazeDensity == 0.02f) && (Fog.GroundHazeThickness == 15.0f));
    // With a ground haze: the densities add up, the thickness is their mean weighed by the densities.
    Environment.GroundHazeDensity = 0.01f;
    Environment.GroundHazeThickness = 60.0f;
    WorldAtmosphere_GetHaze(&Environment, 1.0f, &Fog);
    TEST_CHECK(run, IsClose(Fog.GroundHazeDensity, 0.03f));
    TEST_CHECK(run, IsClose(Fog.GroundHazeThickness, (0.01f * 60.0f + 0.02f * 15.0f) / 0.03f));
    // Absurd values stay within the ground haze's limits.
    Environment.FogDensity = 1.0e30f;
    Environment.GroundHazeDensity = 1.0e30f;
    WorldAtmosphere_GetHaze(&Environment, 1.0f, &Fog);
    TEST_CHECK(run, (Fog.GroundHazeDensity == (float)WORLD_ENVIRONMENT_MAX_GROUND_HAZE_DENSITY)
        && isfinite(Fog.GroundHazeThickness));
    WorldEnvironment_Deconstruct(&Environment);
}


// Public functions.
void WorldWeatherLightTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "no_weather_leaves_the_light_alone", NoWeatherLeavesTheLightAlone },
        { "clouds_cover_and_transmit_by_their_depth", CloudsCoverAndTransmitByTheirDepth },
        { "the_deck_sends_down_what_the_clouds_pass", TheDeckSendsDownWhatTheCloudsPass },
        { "an_overcast_day_is_daylight", AnOvercastDayIsDaylight },
        { "the_flash_falls_off_with_distance", TheFlashFallsOffWithDistance },
        { "broken_values_count_as_none", BrokenValuesCountAsNone },
        { "the_wind_points_where_it_blows", TheWindPointsWhereItBlows },
        { "the_weathers_fog_joins_the_ground_haze", TheWeathersFogJoinsTheGroundHaze },
    };
    TestRun_RunSuite(run, "WorldWeatherLight", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
