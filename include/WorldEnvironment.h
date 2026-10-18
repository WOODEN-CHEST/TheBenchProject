#pragma once
#include <stdint.h>
#include <stdbool.h>
// Color is a Raylib type used for the sky/fog/light colors stored here, so this include is unavoidable.
#include "raylib/raylib.h"
#include "wr/WRError.h"


/**
 * @file WorldEnvironment.h
 * @brief Per-world, player-editable environment and graphics settings (persisted with the world).
 *
 * A world carries an environment: the settings that let each player-authored world look different — the
 * physically based sky and sun, the day-night cycle, the night sky (stars, Milky Way, light pollution), fog, and the
 * world-side
 * multipliers for the post effects (bloom, sunshafts, shadows) that combine with the game config's
 * multipliers. These are DATA ONLY; the world renderer reads them, but this module has no rendering
 * logic and no dependency on the renderer.
 *
 * The struct is a value type that currently owns no heap memory. Copy it with WorldEnvironment_CopyTo rather
 * than by assignment, so a future owned field (a skybox name, a LUT path) cannot turn copies into double
 * frees. It is deliberately expandable: to add a setting, add a field, give it a default in
 * WorldEnvironment_SetDefaults, a rule in WorldEnvironment_Validate (if it has one) and copy it in
 * WorldEnvironment_CopyTo. Effect strengths are stored as multipliers so a world can dial an effect down to 0
 * or boost it; the renderer multiplies them with the game config's own multipliers.
 *
 * Data from outside the program (a save, an editor, a downloaded story) must pass WorldEnvironment_Validate
 * before it is used: the field comments below state the rules it enforces.
 */


// Macros.
/** @brief Time-of-day value (fraction in [0;1]) representing noon; 0/1 is midnight. */
#define WORLD_ENVIRONMENT_TIME_OF_DAY_NOON 0.5f


// Types.
/**
 * @brief The environment/graphics settings of a single world.
 *
 * A value type; populate with WorldEnvironment_SetDefaults, then adjust individual fields. Copy with
 * WorldEnvironment_CopyTo. Release with WorldEnvironment_Deconstruct (currently a no-op, present for
 * future-proofing).
 */
typedef struct WorldEnvironmentStruct
{
    // ---- Sky, sun and day-night cycle ----
    /** @brief Current time of day as a fraction in [0;1]: 0 = midnight, 0.5 = noon. Drives the sun position.
     *  Finite and in [0;1]. */
    float TimeOfDay;
    /** @brief When true, TimeOfDay advances automatically over DayLengthSeconds; when false it is held fixed. */
    bool IsDayNightCycleEnabled;
    /** @brief Real-time length of a full day-night cycle, in seconds. Finite and > 0. */
    float DayLengthSeconds;
    /** @brief Tilt of the sun's arc away from the zenith at noon, in radians. 0 makes the noon sun pass
     *  straight overhead; larger values lean the noon sun toward the horizon (elevation at noon = 90deg minus
     *  this angle). Sunrise and sunset stay on the horizon regardless. Finite; typically [0, PI/2). */
    float SunAngle;

    /** @brief Atmospheric turbidity (haziness) for the physically based sky; higher is hazier. Finite and >= 0. */
    float SkyTurbidity;
    /** @brief Ground albedo color feeding atmospheric scattering (affects horizon/sky tint). Alpha ignored. */
    Color SkyGroundAlbedo;
    /** @brief Tint applied to the computed sky color, so worlds can push alien skies. Alpha ignored. */
    Color SkyTint;

    /** @brief Sun disc color. Alpha ignored. */
    Color SunColor;
    /** @brief Sun radiance multiplier. Finite and >= 0. */
    float SunIntensity;
    /** @brief Multiplier on the sun disc's apparent angular size. Finite and > 0. */
    float SunSizeMultiplier;

    // ---- Night sky: stars, Milky Way, light pollution ----
    /** @brief Seed of the generated star catalogue; the same seed gives the same stars. */
    uint64_t StarSeed;
    /** @brief How many stars the sky has, relative to an Earth-like sky: 1 gives about 9000 stars (what the naked eye
     *  sees from a dark site), 0 none; the catalogue is capped at 20000 stars. Finite and >= 0. */
    float StarDensity;
    /** @brief Brightness multiplier for stars (1 = their natural brightness). Finite and >= 0. */
    float StarBrightness;
    /** @brief How strongly stars twinkle, 0 (steady) to 1; they twinkle more near the horizon. Finite and in [0, 1]. */
    float StarTwinkle;
    /** @brief Brightness multiplier for the Milky Way (1 = natural). Finite and >= 0. */
    float MilkyWayBrightness;
    /** @brief Light pollution from nearby towns, 0 (a dark site) to 1 (a city centre). It hides faint stars (at 1 only
     *  the brightest few remain), washes out the Milky Way, adds a glow near the horizon at night and brightens the
     *  night ambient a little. Finite and in [0, 1]. */
    float LightPollution;
    /** @brief Colour of the light-pollution glow (sodium orange by default). Alpha ignored. */
    Color LightPollutionColor;

    // ---- Ambient / skylight ----
    /** @brief Tint on the ambient skylight applied to shaded surfaces. The renderer picks the ambient HUE from
     *  the sun's elevation to track the sky (blue by day, warm at sunrise/sunset, fading to a faint blue at
     *  night); this colour multiplies that, so WHITE (the default) leaves it as-is and other colours warm/cool
     *  the whole fill. Alpha ignored. */
    Color AmbientSkylightColor;
    /** @brief Ambient skylight intensity multiplier. Finite and >= 0. */
    float AmbientSkylightIntensity;

    // ---- Fog (fades distant objects into the sky) ----
    /** @brief Legacy flat fog colour. NOTE: the renderer's distance fog now fades distant geometry into the
     *  atmospheric SKY colour along each view ray (so far objects dissolve seamlessly into the sky), and no
     *  longer reads this field; it is retained for data/back-compat and possible non-atmospheric fallbacks.
     *  Alpha ignored. */
    Color FogColor;
    /** @brief World-side fog strength multiplier (combined with the game config's fog strength). Finite and >= 0. */
    float FogStrength;

    // ---- Post-effect world multipliers (combined with the game config multipliers) ----
    /** @brief When false, this world suppresses bloom regardless of the config. */
    bool IsBloomEnabled;
    /** @brief World-side bloom strength multiplier (multiplied by the config's bloom strength). Finite and >= 0. */
    float BloomStrength;

    /** @brief When false, this world suppresses sunshafts regardless of the config. */
    bool AreSunshaftsEnabled;
    /** @brief World-side sunshaft strength multiplier (multiplied by the config's sunshaft strength). Finite and >= 0. */
    float SunshaftStrength;

    /** @brief When false, this world suppresses object shadows regardless of the config. */
    bool AreShadowsEnabled;
    /** @brief World-side shadow intensity multiplier (multiplied by the config's shadow strength). Finite and >= 0. */
    float ShadowStrength;

    /** @brief When false, this world suppresses screen-space ambient occlusion regardless of the config. */
    bool IsAmbientOcclusionEnabled;
    /** @brief World-side ambient-occlusion strength multiplier (multiplied by the config's AO strength).
     *  Scales how strongly the screen-space AO darkens creased/contact areas. Finite and >= 0. */
    float AmbientOcclusionStrength;
} WorldEnvironment;


// Functions.
/**
 * @brief Resets an environment to its built-in defaults (a clear daytime sky, gentle effects).
 * @param self The environment to populate. May be NULL, in which case the call does nothing.
 */
void WorldEnvironment_SetDefaults(WorldEnvironment* self);

/**
 * @brief Checks every field against the rules stated in the field comments.
 *
 * Use it on any environment that came from outside the program (a save file, an editor, downloaded content)
 * before applying it to a world: a NaN or infinite value would otherwise poison the time of day, the sun
 * direction and the lighting permanently. Colors, booleans and the star seed accept every value.
 * @param self The environment to check; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange naming the
 *          first field that breaks its rule.
 */
Error WorldEnvironment_Validate(const WorldEnvironment* self);

/**
 * @brief Copies an environment into another one, replacing the destination's settings.
 *
 * Use this instead of struct assignment. Today it is a plain copy; when the environment gains owned fields,
 * this is the one place that deep-copies them and releases what the destination held.
 * @param self The environment to copy; must not be NULL.
 * @param destination The environment to overwrite (populated with WorldEnvironment_SetDefaults or an earlier
 *        copy); must not be NULL. May equal @p self (then nothing changes).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p destination is NULL.
 */
Error WorldEnvironment_CopyTo(const WorldEnvironment* self, WorldEnvironment* destination);

/**
 * @brief Releases any resources owned by an environment.
 *
 * Present for symmetry and future-proofing; a WorldEnvironment currently owns nothing, so this is a
 * no-op. Safe to call on any environment, including NULL.
 * @param self The environment to release, or NULL.
 */
void WorldEnvironment_Deconstruct(WorldEnvironment* self);

/**
 * @brief Returns the unit direction pointing TO the sun for the current time of day.
 *
 * The sun arcs overhead: at dawn (TimeOfDay 0.25) and dusk (0.75) it lies on the horizon; at midnight it
 * points below. The east-west sweep is along the X axis. SunAngle tilts the arc out of vertical (around the
 * X axis, into Z), so at noon the sun sits SunAngle radians from the zenith rather than straight up (0 =
 * straight overhead). This is the light direction the sky/shadow/lighting passes use.
 * @param self The environment; must not be NULL.
 * @returns The normalized direction to the sun.
 */
Vector3 WorldEnvironment_GetSunDirection(const WorldEnvironment* self);

/**
 * @brief Returns the rotation from the celestial frame into the world for the current time of day.
 *
 * The celestial frame turns with the sky: the sun sits at its point (1,0,0) (so this matrix applied to (1,0,0) equals
 * WorldEnvironment_GetSunDirection), and the daily rotation turns it around its z axis. SunAngle acts as the latitude:
 * the celestial pole above the horizon is -z, which lies at world (0, sin SunAngle, -cos SunAngle) at every time of
 * day, so the sky turns around a fixed point that low over the horizon toward -Z. Stars and other objects fixed among
 * the stars are placed in this frame and turned into the world with this matrix once per frame.
 * The matrix is R_x(SunAngle) . R_z((TimeOfDay - 0.25) . 2pi), in raymath's convention (Vector3Transform applies it).
 * @param self The environment; must not be NULL.
 * @returns The celestial-to-world rotation (orthonormal, no translation).
 */
Matrix WorldEnvironment_GetCelestialToWorld(const WorldEnvironment* self);

/**
 * @brief Returns how much of the sun's light reaches the world right now: 1 in full day, 0 at night.
 *
 * A smooth 0..1 factor derived from the sun's elevation (the Y of WorldEnvironment_GetSunDirection): it is 1
 * while the sun is well above the horizon, ramps down through twilight as the sun approaches and crosses the
 * horizon, and is 0 once the sun is below it. The renderer multiplies the sun's intensity by this (so the sun
 * stops lighting objects at night) and blends the ambient skylight from its daytime color toward a dim night
 * ambient by the same factor, so the world actually darkens over the day-night cycle.
 * @param self The environment; must not be NULL.
 * @returns The daylight factor in [0, 1].
 */
float WorldEnvironment_GetDaylightFactor(const WorldEnvironment* self);

/**
 * @brief Advances the time of day by @p deltaSeconds when the day-night cycle is enabled.
 *
 * Adds @p deltaSeconds / DayLengthSeconds to TimeOfDay and wraps the result into [0, 1). Does nothing if
 * the cycle is disabled, DayLengthSeconds is not a finite positive number, @p deltaSeconds is negative or not
 * finite, or the step would not give a finite time, so a bad input can never leave TimeOfDay NaN.
 * @param self The environment; may be NULL (no-op).
 * @param deltaSeconds Elapsed real seconds to advance by; must be finite and non-negative to have an effect.
 */
void WorldEnvironment_Advance(WorldEnvironment* self, float deltaSeconds);

/**
 * @brief Computes the sky/background color for the current time of day (a day-night gradient, tinted).
 *
 * Blends between day, horizon (sunrise/sunset) and night colors by the sun's elevation, then multiplies by
 * SkyTint so a world can push the sky toward alien hues. This is a cheap CPU approximation; the full
 * atmospheric-scattering sky pass replaces it later but is driven by the same time-of-day.
 * @param self The environment; must not be NULL.
 * @returns The opaque sky color.
 */
Color WorldEnvironment_ComputeSkyColor(const WorldEnvironment* self);
