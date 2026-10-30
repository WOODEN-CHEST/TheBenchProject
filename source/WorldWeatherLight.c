// The weather's look as pure maths (WorldWeatherLight.h): the clouds' deck over the sky's light, the flash, the wetness
// and the wind, for the renderer.

#include <math.h>
#include "WorldWeatherLight.h"
#include "wr/WRMemory.h"


// Macros.
/** Pi, in single precision. */
#define WEATHER_PI 3.14159265358979323846f
/** The overcast sky (CIE): radiance (1 + 2 cos zenith) / 3 of the zenith's, whose irradiance on a level surface is
 *  7 pi / 9 of the zenith's radiance, and whose mean radiance over its half of the sky is 2 / 3 of it. */
#define OVERCAST_ZENITH_PER_IRRADIANCE (9.0f / (7.0f * WEATHER_PI))
#define OVERCAST_MEAN_PER_ZENITH (2.0f / 3.0f)
/** The flash's colour (linear): a blue-white. */
#define FLASH_RED 0.82f
#define FLASH_GREEN 0.88f
#define FLASH_BLUE 1.0f


// Static functions.
/* A finite value in [minimum, maximum]; a non-finite one counts as the minimum (none). */
static float ClampWeather(float value, float minimum, float maximum)
{
    return isfinite(value) ? fminf(fmaxf(value, minimum), maximum) : minimum;
}


// Public functions.
void WorldWeatherLight_GetFrame(const WorldEnvironment* environment, WorldWeatherLight* outLight)
{
    Memory_Zero(outLight, sizeof(*outLight));
    float Coverage = ClampWeather(environment->CloudCoverage, 0.0f, 1.0f);
    float Density = ClampWeather(environment->CloudDensity, 0.0f, 1.0f);
    float Depth = WORLD_WEATHER_CLOUD_DEPTH_PER_DENSITY * Density;
    outLight->CloudCover = Coverage * -expm1f(-Depth);
    outLight->DirectShare = 1.0f - outLight->CloudCover;
    outLight->CloudTransmission = 1.0f / (1.0f + 0.75f * (1.0f - WORLD_WEATHER_CLOUD_ASYMMETRY) * Depth);

    float Flash = ClampWeather(environment->LightningFlash, 0.0f, 1.0f);
    float Distance = ClampWeather(environment->LightningDistance, WORLD_WEATHER_FLASH_NEAREST_DISTANCE,
        (float)WORLD_ENVIRONMENT_MAX_DISTANCE);
    float Irradiance = fminf(WORLD_WEATHER_FLASH_IRRADIANCE * WORLD_WEATHER_FLASH_REFERENCE_DISTANCE / Distance,
        WORLD_WEATHER_FLASH_MAX_IRRADIANCE) * Flash;
    outLight->FlashIrradiance[0] = Irradiance * FLASH_RED;
    outLight->FlashIrradiance[1] = Irradiance * FLASH_GREEN;
    outLight->FlashIrradiance[2] = Irradiance * FLASH_BLUE;
    Vector3 Level = environment->LightningDirection;
    Level.y = 0.0f;
    float LevelLength = sqrtf(Level.x * Level.x + Level.z * Level.z);
    Level = (isfinite(LevelLength) && (LevelLength > 0.0f))
        ? (Vector3){ Level.x / LevelLength, 0.0f, Level.z / LevelLength } : (Vector3){ 0.0f, 0.0f, 1.0f };
    Vector3 Glow = { Level.x * Distance, WORLD_WEATHER_CLOUD_HEIGHT, Level.z * Distance };
    float GlowLength = sqrtf(Glow.x * Glow.x + Glow.y * Glow.y + Glow.z * Glow.z);
    outLight->FlashDirection = (Vector3){ Glow.x / GlowLength, Glow.y / GlowLength, Glow.z / GlowLength };

    outLight->Wetness = ClampWeather(environment->Wetness, 0.0f, 1.0f);
    float WindAngle = isfinite(environment->WindDirection) ? environment->WindDirection : 0.0f;
    outLight->WindDirection = (Vector3){ sinf(WindAngle), 0.0f, cosf(WindAngle) };
    outLight->WindSpeed = ClampWeather(environment->WindSpeed, 0.0f, (float)WORLD_ENVIRONMENT_MAX_WIND_SPEED);
    outLight->WindGust = ClampWeather(environment->WindGust, 0.0f, (float)WORLD_ENVIRONMENT_MAX_WIND_SPEED);
}

bool WorldWeatherLight_HasClouds(const WorldWeatherLight* self)
{
    return self->CloudCover > 0.0f;
}

bool WorldWeatherLight_IsVisible(const WorldWeatherLight* self)
{
    return WorldWeatherLight_HasClouds(self) || (self->Wetness > 0.0f) || (self->FlashIrradiance[0] > 0.0f)
        || (self->FlashIrradiance[1] > 0.0f) || (self->FlashIrradiance[2] > 0.0f);
}

void WorldWeatherLight_AddDeck(WorldWeatherLight* self, const WorldSkyLightFrame* cloudTop, Vector3 sunDirection,
    Vector3 moonDirection, const float groundAlbedo[3], WorldSkyLightFrame* frame)
{
    float SunLevel = fmaxf(sunDirection.y, 0.0f);
    float MoonLevel = fmaxf(moonDirection.y, 0.0f);
    for (int Channel = 0; Channel < 3; Channel++)
    {
        float Albedo = isfinite(groundAlbedo[Channel]) ? fminf(fmaxf(groundAlbedo[Channel], 0.0f), 1.0f) : 0.0f;
        // What falls on the clouds' top: the sun's and the moon's light on a level surface and the sky's own.
        float OnTop = cloudTop->Sun[Channel] * SunLevel + cloudTop->Moon[Channel] * MoonLevel + cloudTop->Sky[Channel];
        float Deck = self->CloudCover * self->CloudTransmission * OnTop;
        Deck = isfinite(Deck) ? fminf(fmaxf(Deck, 0.0f), WORLD_SKY_LIGHT_MAX_LIGHT) : 0.0f;
        self->DeckIrradiance[Channel] = Deck;
        frame->Sky[Channel] = fminf(frame->Sky[Channel] + Deck, WORLD_SKY_LIGHT_MAX_LIGHT);
        frame->Ground[Channel] = fminf(frame->Ground[Channel] + Albedo * Deck, WORLD_SKY_LIGHT_MAX_LIGHT);
        // The sun's part of it is daylight (the sky's SunSky), so the eye and night vision read an overcast day as day.
        float DeckDay = self->CloudCover * self->CloudTransmission
            * (cloudTop->Sun[Channel] * SunLevel + cloudTop->SunSky[Channel]);
        DeckDay = isfinite(DeckDay) ? fminf(fmaxf(DeckDay, 0.0f), Deck) : 0.0f;
        frame->SunSky[Channel] = fminf(frame->SunSky[Channel] + DeckDay, WORLD_SKY_LIGHT_MAX_LIGHT);
        // The sky shows the deck: its zenith's radiance for the irradiance it gives (the flash's own glow is the sky
        // pass's).
        self->ZenithRadiance[Channel] = Deck * OVERCAST_ZENITH_PER_IRRADIANCE;
        // The air below is lit from all around: the deck's mean radiance over the upper half and the ground's bounce of
        // the deck's and the flash's light over the lower half.
        float Lit = Deck + self->FlashIrradiance[Channel];
        self->AmbientRadiance[Channel] = 0.5f * (Lit * OVERCAST_ZENITH_PER_IRRADIANCE * OVERCAST_MEAN_PER_ZENITH
            + Albedo * Lit / WEATHER_PI);
    }
}
