#pragma once
#include <stdint.h>
#include <stdbool.h>
// Color is a Raylib type used for the sky/fog/light colors stored here, so this include is unavoidable.
#include "raylib/raylib.h"


/**
 * @file WorldEnvironment.h
 * @brief Per-world, player-editable environment and graphics settings (persisted with the world).
 *
 * A world carries an environment: the settings that let each player-authored world look different — the
 * physically based sky and sun, the day-night cycle, the night star field, fog, and the world-side
 * multipliers for the post effects (bloom, sunshafts, shadows) that combine with the game config's
 * multipliers. These are DATA ONLY; the world renderer reads them, but this module has no rendering
 * logic and no dependency on the renderer.
 *
 * The struct is a plain value type owning no heap memory (copy it freely). It is deliberately expandable:
 * to add a setting, add a field and give it a default in WorldEnvironment_SetDefaults. Effect strengths
 * are stored as multipliers so a world can dial an effect down to 0 or boost it; the renderer multiplies
 * them with the game config's own multipliers.
 */


// Macros.
/** @brief Time-of-day value (fraction in [0;1]) representing noon; 0/1 is midnight. */
#define WORLD_ENVIRONMENT_TIME_OF_DAY_NOON 0.5f


// Types.
/**
 * @brief The environment/graphics settings of a single world.
 *
 * A plain value type; populate with WorldEnvironment_SetDefaults, then adjust individual fields. Release
 * with WorldEnvironment_Deconstruct (currently a no-op, present for future-proofing).
 */
typedef struct WorldEnvironmentStruct
{
    // ---- Sky, sun and day-night cycle ----
    /** @brief Current time of day as a fraction in [0;1]: 0 = midnight, 0.5 = noon. Drives the sun position. */
    float TimeOfDay;
    /** @brief When true, TimeOfDay advances automatically over DayLengthSeconds; when false it is held fixed. */
    bool IsDayNightCycleEnabled;
    /** @brief Real-time length of a full day-night cycle, in seconds. Must be > 0 to advance. */
    float DayLengthSeconds;

    /** @brief Atmospheric turbidity (haziness) for the physically based sky; higher is hazier. */
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

    // ---- Night stars (consistent positions from a seed) ----
    /** @brief Seed used to place the (consistent) night star field; the same seed gives the same stars. */
    uint64_t StarSeed;
    /** @brief Relative density of stars in the night sky. Finite and >= 0. */
    float StarDensity;
    /** @brief Brightness multiplier for stars. Finite and >= 0. */
    float StarBrightness;

    // ---- Ambient / skylight ----
    /** @brief Ambient skylight color applied to shaded surfaces. Alpha ignored. */
    Color AmbientSkylightColor;
    /** @brief Ambient skylight intensity multiplier. Finite and >= 0. */
    float AmbientSkylightIntensity;

    // ---- Fog (fades distant objects into the sky) ----
    /** @brief Fog color; distant objects fade toward this (usually matched to the sky). Alpha ignored. */
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
} WorldEnvironment;


// Functions.
/**
 * @brief Resets an environment to its built-in defaults (a clear daytime sky, gentle effects).
 * @param self The environment to populate. May be NULL, in which case the call does nothing.
 */
void WorldEnvironment_SetDefaults(WorldEnvironment* self);

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
 * The sun arcs overhead: at noon (TimeOfDay 0.5) it points straight up (+Y); at dawn (0.25) and dusk
 * (0.75) it lies on the horizon; at midnight it points below. The horizontal component sweeps along the X
 * axis. This is the light direction the later sky/shadow/lighting passes use.
 * @param self The environment; must not be NULL.
 * @returns The normalized direction to the sun.
 */
Vector3 WorldEnvironment_GetSunDirection(const WorldEnvironment* self);

/**
 * @brief Advances the time of day by @p deltaSeconds when the day-night cycle is enabled.
 *
 * Adds @p deltaSeconds / DayLengthSeconds to TimeOfDay and wraps the result into [0, 1). Does nothing if
 * the cycle is disabled or DayLengthSeconds is not positive.
 * @param self The environment; may be NULL (no-op).
 * @param deltaSeconds Elapsed real seconds to advance by; finite and non-negative expected.
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
