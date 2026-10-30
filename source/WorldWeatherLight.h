#pragma once
#include <stdbool.h>
// Vector3 is raylib's: the flash's and the wind's directions.
#include "raylib/raylib.h"
#include "WorldEnvironment.h"
#include "WorldSkyLight.h"

/*
 * Private header (source/): the weather's look as pure maths, the renderer's side of the weather's values (the
 * environment's, WeatherSystem.h). Tested in tests/WorldWeatherLightTests.c. The renderer builds one WorldWeatherLight a
 * frame and feeds it to the atmosphere passes, the sky pass and the lit shaders as uniforms (agents/WorldRuntime.md,
 * "Weather").
 *
 * THE CLOUDS, until clouds are drawn: one even deck over the whole sky, a share of it (the coverage) covered. A cloud's
 * optical depth is WORLD_WEATHER_CLOUD_DEPTH_PER_DENSITY x cloud_density (1: a storm cloud's 64), so the covered share
 * the eye and the sun see through is coverage x (1 - e^-depth): the CLOUD COVER. Below the deck:
 *  - the sun's and the moon's light (on surfaces, in the air, on the far ground) is what the cover leaves: 1 - cover
 *    (the renderer dims the lights above the air by it, so everything they light dims alike);
 *  - the covered share sends down the light that falls on the clouds' top (the sun's and the moon's on a level surface
 *    and the sky's own) times the clouds' TRANSMISSION, diffused (two-stream: 1 / (1 + 0.75 (1 - g) depth), g 0.85: a
 *    thin deck passes most, a storm's about an eighth): the DECK'S IRRADIANCE on a level surface. It joins the sky's
 *    irradiance on surfaces (the ambient: the share shifts from the direct light to it as the clouds thicken), the
 *    ground's bounce, and the air below the deck is lit by it from all around (AMBIENT RADIANCE: the deck's mean radiance
 *    over its half of the sky, the overcast sky's (1 + 2 cos zenith) / 3, and the ground's bounce of it below);
 *  - the sky shows the deck at the clouds' height (WORLD_WEATHER_CLOUD_HEIGHT): its radiance at the zenith (ZENITH
 *    RADIANCE, the overcast sky's) falling to a third at the horizon, through the air below it, on top of the clear sky
 *    the cover left. So the sky greys and darkens as
 *    the clouds thicken, and far hills fade into a grey haze that meets it at the horizon.
 * The deck is everywhere above: a camera above real clouds' height still sees it (drawn clouds are the clouds' own task).
 *
 * THE FLASH: a lightning strike lights the clouds over a wide area, so its light on a level surface falls off as
 * 1 / distance (not 1 / distance^2: an area that grows with the distance lights it), WORLD_WEATHER_FLASH_IRRADIANCE
 * at a kilometre, blue-white, at most WORLD_WEATHER_FLASH_MAX_IRRADIANCE; it joins the sky's light on surfaces and in
 * the air, and the sky glows with it, most toward the strike (the clouds' height, WORLD_WEATHER_CLOUD_HEIGHT, over it). The
 * eye's anchor and night vision's field leave it out: a fraction of a second is too short to adapt to.
 *
 * WETNESS darkens porous surfaces and makes them glossier, most on surfaces facing up (common/lighting's
 * ApplyWetness). THE WIND (direction, speed, gust) is kept for what sways in it.
 */


// Macros.
/** A cloud's optical depth per unit of cloud_density: density 1 is a storm cloud's depth. */
#define WORLD_WEATHER_CLOUD_DEPTH_PER_DENSITY 64.0f
/** The clouds' forward scattering (the asymmetry g of their droplets) in the two-stream transmission. */
#define WORLD_WEATHER_CLOUD_ASYMMETRY 0.85f
/** The flash's light on a level surface from a strike a kilometre away, in the surfaces' light units, and the most it
 *  gives (a strike next door). */
#define WORLD_WEATHER_FLASH_IRRADIANCE 0.1f
#define WORLD_WEATHER_FLASH_MAX_IRRADIANCE 0.5f
/** The distance the flash's falloff is measured from, metres, and the nearest it counts. */
#define WORLD_WEATHER_FLASH_REFERENCE_DISTANCE 1000.0f
#define WORLD_WEATHER_FLASH_NEAREST_DISTANCE 200.0f
/** The clouds' height above the atmosphere's ground, metres: where the sky shows the deck (through the air below it)
 *  and where a flash's glow sits over its strike. */
#define WORLD_WEATHER_CLOUD_HEIGHT 1500.0f


// Types.
/** The frame's weather as the renderer draws it. A plain value; WorldWeatherLight_GetFrame fills it. */
typedef struct WorldWeatherLightStruct
{
    /** @brief The share of the sky the clouds hide (coverage x their opacity), 0..1: the sky shows the deck there, and
     *  the discs, the stars and the Milky Way are dimmed by it. */
    float CloudCover;
    /** @brief The share of the sun's and the moon's light that still comes through: 1 - CloudCover. */
    float DirectShare;
    /** @brief The share of the light falling on the clouds' top they pass down, diffused (two-stream), 0..1. */
    float CloudTransmission;
    /** @brief The deck's diffuse light on a level surface below it, linear (the surfaces' units; WorldWeatherLight_AddDeck
     *  sets it, 0 before). */
    float DeckIrradiance[3];
    /** @brief The light around a point of the air below the deck, from all directions (its mean radiance), linear; the
     *  flash's in (WorldWeatherLight_AddDeck sets it, 0 before). */
    float AmbientRadiance[3];
    /** @brief The deck's radiance at the zenith as the sky shows it (the cover in), linear. */
    float ZenithRadiance[3];
    /** @brief The flash's light on a level surface now, linear (0 without one), and the unit direction toward its glow. */
    float FlashIrradiance[3];
    Vector3 FlashDirection;
    /** @brief How wet surfaces are, 0..1. */
    float Wetness;
    /** @brief The wind: the level unit direction it blows toward (world space: (sin, 0, cos) of wind_direction), its mean
     *  speed and the current gust on top, metres per second. */
    Vector3 WindDirection;
    float WindSpeed;
    float WindGust;
} WorldWeatherLight;


// Functions.
/** Fills the frame's weather from the environment's values (the cover, the transmission, the flash, the wetness, the
 *  wind; non-finite values count as none); the deck's light and the air's come with WorldWeatherLight_AddDeck. */
void WorldWeatherLight_GetFrame(const WorldEnvironment* environment, WorldWeatherLight* outLight);

/** Whether the frame has clouds: CloudCover above 0 (else the light is the clear sky's, untouched). */
bool WorldWeatherLight_HasClouds(const WorldWeatherLight* self);

/** Whether anything of the weather shows: clouds, a flash or wet surfaces. */
bool WorldWeatherLight_IsVisible(const WorldWeatherLight* self);

/**
 * Adds the deck's light to the sky's light at the camera. @p cloudTop is the light falling on the clouds' top (the clear
 * sky's frame light at WORLD_WEATHER_CLOUD_HEIGHT, for the lights above the air), @p frame the camera's with the lights
 * dimmed by DirectShare (the clear sky's without clouds): its sky's irradiance takes the deck's, its daylight's part
 * (SunSky) the share the sun gave it, its ground's bounce the ground's share of it. Sets the deck's
 * irradiance, the zenith radiance and the ambient radiance (the flash's light in it). @p groundAlbedo is the ground's
 * (linear). Without clouds @p frame stays as it is, bit for bit.
 */
void WorldWeatherLight_AddDeck(WorldWeatherLight* self, const WorldSkyLightFrame* cloudTop, Vector3 sunDirection,
    Vector3 moonDirection, const float groundAlbedo[3], WorldSkyLightFrame* frame);
