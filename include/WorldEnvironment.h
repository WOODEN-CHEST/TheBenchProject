#pragma once
#include <stdint.h>
#include <stdbool.h>
// Color is a Raylib type used for the sky/fog/light colors stored here, so this include is unavoidable.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRJSON.h"
#include "PropertyDescriptor.h"


/**
 * @file WorldEnvironment.h
 * @brief Per-world, player-editable environment and graphics settings (persisted with the world).
 *
 * A world carries an environment: the settings that let each player-authored world look different — the
 * physically based sky and sun, the night sky (stars, Milky Way, light pollution), fog, and the world-side
 * multipliers for the post effects (bloom, sunshafts, shadows) that combine with the game config's
 * multipliers. These are DATA ONLY; the world renderer reads them, but this module has no rendering
 * logic and no dependency on the renderer.
 *
 * The date and the time of day here are what the sky SHOWS, not a clock: nothing in this module advances them.
 * While a game runs, the game clock (GameClock.h) writes both every tick; in a world file they are where the sky
 * starts (the editor's preview uses them, and a game may start its clock from them).
 *
 * Every field is described once in the environment's property table (WorldEnvironment_GetPropertyTable, in
 * source/WorldEnvironment.c): its key (the field name in snake_case, e.g. "light_pollution"), its GHDF wire id, its
 * default and its rules. The functions below are thin wrappers over the generic property functions
 * (PropertyDescriptor.h), so defaults, validation, copying, JSON and GHDF all come from that one table. To add a
 * setting, add the field and one table row with a new, never used wire id. Effect strengths are stored as
 * multipliers so a world can dial an effect down to 0 or boost it; the renderer multiplies them with the game
 * config's own multipliers.
 *
 * The struct currently owns no heap memory, but treat it as if it did: construct it with WorldEnvironment_SetDefaults,
 * copy it with WorldEnvironment_CopyTo rather than by assignment, and release it with WorldEnvironment_Deconstruct,
 * so an owned field (a texture name) can be added without turning copies into double frees.
 *
 * Data from outside the program (a save, an editor, a downloaded story) must pass WorldEnvironment_Validate
 * before it is used (the readers do it themselves): the field comments below state the rules it enforces.
 */


// Macros.
/** @brief Time-of-day value (fraction in [0;1]) representing noon; 0/1 is midnight. */
#define WORLD_ENVIRONMENT_TIME_OF_DAY_NOON 0.5f


// Types.
/**
 * @brief The environment/graphics settings of a single world.
 *
 * Populate with WorldEnvironment_SetDefaults, then adjust individual fields. Copy with WorldEnvironment_CopyTo.
 * Release with WorldEnvironment_Deconstruct (nothing to free yet, but pair it with every SetDefaults).
 */
typedef struct WorldEnvironmentStruct
{
    // ---- Date, time of day, sky and sun ----
    /** @brief The time of day the sky shows, as a fraction in [0;1]: 0 = midnight, 0.5 = noon. Drives the sun
     *  position. A running game's clock writes it every tick (GameClock_GetTimeOfDay). Finite and in [0;1]. */
    float TimeOfDay;
    /** @brief The date the sky shows, as a day number of the game calendar (GameCalendar.h: days since 0001-01-01):
     *  the moon's phase and the stars' drift through the year follow it. A running game's clock writes it every tick
     *  (GameClock_GetDay). Inside the calendar (years 1 to 9999). */
    int64_t Date;
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
 * @brief Returns the environment's property table: every field's key, wire id, type, default, rules and flags.
 *
 * Generic code (the world encoder, the editor's inspector, scripts, tests) works on environments through it with the
 * Properties_* functions.
 * @returns The table; static, never NULL.
 */
const PropertyTable* WorldEnvironment_GetPropertyTable(void);

/**
 * @brief Constructs an environment with its built-in defaults (a clear daytime sky, gentle effects): the table's
 *        Properties_Construct.
 *
 * Call it on a new environment and pair it with WorldEnvironment_Deconstruct; on one that holds owned data, release
 * that first.
 * @param self The environment to populate. May be NULL, in which case the call does nothing.
 */
void WorldEnvironment_SetDefaults(WorldEnvironment* self);

/**
 * @brief Checks every field against the rules stated in the field comments (the table's Properties_Validate).
 *
 * Use it on any environment that came from outside the program (a save file, an editor, downloaded content)
 * before applying it to a world: a NaN or infinite value would otherwise poison the time of day, the sun
 * direction and the lighting permanently. Colors, booleans and the star seed accept every value; the date must lie in
 * the calendar.
 * @param self The environment to check; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange naming the first field
 *          that breaks its rule, by key and by C name, e.g. "world_environment.light_pollution (LightPollution) must
 *          be finite and in [0, 1]."
 */
Error WorldEnvironment_Validate(const WorldEnvironment* self);

/**
 * @brief Copies an environment into another one, replacing the destination's settings (a deep copy, the table's
 *        Properties_CopyTo).
 *
 * Use this instead of struct assignment, so owned fields are cloned and what the destination held is released.
 * @param self The environment to copy; must not be NULL.
 * @param destination The environment to overwrite (populated with WorldEnvironment_SetDefaults or an earlier
 *        copy); must not be NULL. May equal @p self (then nothing changes).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p destination is NULL.
 */
Error WorldEnvironment_CopyTo(const WorldEnvironment* self, WorldEnvironment* destination);

/**
 * @brief Releases any resources owned by an environment (the table's Properties_Deconstruct).
 *
 * An environment owns nothing yet; call it anyway, so owned fields can be added. Safe to call on any constructed
 * environment, also more than once, and on NULL.
 * @param self The environment to release, or NULL.
 */
void WorldEnvironment_Deconstruct(WorldEnvironment* self);

/**
 * @brief Reads an environment from a JSON compound (the table's Properties_ReadJSON): all or nothing.
 *
 * Keys are the fields' snake_case names; absent keys give the defaults; colors are "#rrggbb[aa]" (or any GameJSON
 * color form), the sun angle is in degrees and the date is "yyyy-mm-dd". Everything is validated before @p self
 * changes; on failure it is unchanged.
 * @param self A constructed environment to fill; must not be NULL.
 * @param compound The JSON data; must not be NULL. A migration for older data may change it in place.
 * @param options The read options (unknown keys, data version), or NULL for PropertyReadOptions_CreateDefault (unknown
 *        keys fail the read).
 * @returns Success; ErrorCode_IllegalArgument if @p self or @p compound is NULL; ErrorCode_InvalidJSON naming the
 *          key of a bad or unknown value.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldEnvironment_ReadJSON(WorldEnvironment* self, JSONCompound* compound, const PropertyReadOptions* options);

/**
 * @brief Writes an environment into a JSON compound (the table's Properties_WriteJSON).
 *
 * By default only settings that differ from their defaults are written, so world files stay short.
 * @param self The environment; must not be NULL. It is validated first.
 * @param pool The pool owning @p compound; must not be NULL.
 * @param compound The compound to write into; must not be NULL.
 * @param options The write options, or NULL for PropertyWriteOptions_CreateDefault.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL; ErrorCode_ArgumentOutOfRange for an invalid
 *          environment.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error WorldEnvironment_WriteJSON(const WorldEnvironment* self, JSONObjectPool* pool, JSONCompound* compound,
    const PropertyWriteOptions* options);

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
 * ambient by the same factor, so the world actually darkens as the game clock moves the time of day.
 * @param self The environment; must not be NULL.
 * @returns The daylight factor in [0, 1].
 */
float WorldEnvironment_GetDaylightFactor(const WorldEnvironment* self);

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
