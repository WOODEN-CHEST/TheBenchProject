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
 * physically based sky and sun, the moon, the night sky (stars, Milky Way, light pollution), fog, and the world-side
 * multipliers for the post effects (bloom, sunshafts, shadows) that combine with the game config's
 * multipliers. These are DATA ONLY; the world renderer reads them, but this module has no rendering
 * logic and no dependency on the renderer.
 *
 * The date and the time of day here are what the sky SHOWS, not a clock: nothing in this module advances them.
 * While a game runs, the game clock (GameClock.h) writes both every tick; in a world file they are where the sky
 * starts (the editor's preview uses them, and a game may start its clock from them).
 *
 * THE SKY'S MODEL. Pure functions below turn the date and time into what the sky shows: the sun's direction (on the
 * celestial equator, or following the seasons when AreSeasonsEnabled), the moon (its phase from the calendar, its
 * direction east of the sun by that phase, its lit fraction), the stars' rotation (turning once a day and drifting
 * about 1 degree a day through the year), and the two directional lights with the choice of which one casts the
 * shadows (WorldEnvironment_GetSkyLights).
 *
 * Every field is described once in the environment's property table (WorldEnvironment_GetPropertyTable, in
 * source/WorldEnvironment.c): its key (the field name in snake_case, e.g. "light_pollution"), its GHDF wire id, its
 * default and its rules. The functions below are thin wrappers over the generic property functions
 * (PropertyDescriptor.h), so defaults, validation, copying, JSON and GHDF all come from that one table. To add a
 * setting, add the field and one table row with a new, never used wire id. Effect strengths are stored as
 * multipliers so a world can dial an effect down to 0 or boost it; the renderer multiplies them with the game
 * config's own multipliers.
 *
 * OWNERSHIP. The environment owns its texture names (MilkyWayTexture, MoonTexture): construct it with
 * WorldEnvironment_SetDefaults (which allocates them), copy it with WorldEnvironment_CopyTo, never by assignment (a
 * copied pointer would be freed twice), and release it with WorldEnvironment_Deconstruct.
 *
 * Data from outside the program (a save, an editor, a downloaded story) must pass WorldEnvironment_Validate
 * before it is used (the readers do it themselves): the field comments below state the rules it enforces.
 */


// Macros.
/** @brief Time-of-day value (fraction in [0;1]) representing noon; 0/1 is midnight. */
#define WORLD_ENVIRONMENT_TIME_OF_DAY_NOON 0.5f
/** @brief Day number (GameCalendar.h) of a real new moon, 2000-01-06 18:14 UTC, as a fractional day: with a phase
 *  offset of 0 and the default cycle length, the in-game moon has the real moon's phase for the in-game date (to
 *  within about a day: the real cycle varies, and time zones are not modelled). */
#define WORLD_ENVIRONMENT_MOON_EPOCH_DAY 730124.7597
/** @brief The real moon's mean cycle (synodic month), new moon to new moon, in days: MoonCycleDays's default. */
#define WORLD_ENVIRONMENT_MOON_CYCLE_DAYS 29.530588f
/** @brief MoonIntensity's default: the moonlight of a full moon, relative to the sun's light. Far above the real
 *  moon's (about 1/400000), so that at the night exposure a full-moon night is clearly lighter than a new-moon one,
 *  with soft shadows, and still reads as night (tuned on rendered frames: 8% lit the test world nearly like day). */
#define WORLD_ENVIRONMENT_DEFAULT_MOON_INTENSITY 0.02f
/** @brief The default gravity, in metres per second squared. */
#define WORLD_ENVIRONMENT_DEFAULT_GRAVITY 15.0f
/** @brief The largest gravity a world may set, in metres per second squared (a bound for hostile content). */
#define WORLD_ENVIRONMENT_MAX_GRAVITY 1000.0
/** @brief The most bytes a texture name (MilkyWayTexture, MoonTexture) may have, without the terminator. */
#define WORLD_ENVIRONMENT_TEXTURE_NAME_MAX_LENGTH ((size_t)128)
/** @brief The haze's scale height's limits (HazeHeight), in metres: below 10 m it would fall between the rows of the
 *  renderer's atmosphere tables, and 100 km is the top of the atmosphere. */
#define WORLD_ENVIRONMENT_MIN_HAZE_HEIGHT 10.0
#define WORLD_ENVIRONMENT_MAX_HAZE_HEIGHT 100000.0
/** @brief The largest SunGlow (the haze's forward scattering): at 1 its phase function would be a spike. */
#define WORLD_ENVIRONMENT_MAX_SUN_GLOW 0.99
/** @brief The largest AirDensity and Ozone, as multiples of Earth's (a bound for hostile content: the sky is black long
 *  before). */
#define WORLD_ENVIRONMENT_MAX_AIR_MULTIPLE 100.0


// Types.
/**
 * @brief Where the moon's phase comes from (WorldEnvironment.MoonPhaseSource). The values are saved: never renumber.
 */
typedef enum WorldMoonPhaseSourceEnum
{
    /** @brief From the date: the real moon's phase by default, shifted by MoonPhaseOffsetDays, cycling every
     *  MoonCycleDays. */
    WorldMoonPhaseSource_Calendar = 0,
    /** @brief Always MoonPhase, whatever the date (a story pins a full moon). */
    WorldMoonPhaseSource_Fixed = 1,
} WorldMoonPhaseSource;

/**
 * @brief Which directional light casts the shadows at a moment (WorldSkyLights.ShadowLight). Not saved.
 */
typedef enum WorldShadowLightEnum
{
    /** @brief Neither: both lights are too weak or too low, or about as strong as each other (the shadows fade out
     *  while one takes over from the other). */
    WorldShadowLight_None = 0,
    /** @brief The sun. */
    WorldShadowLight_Sun = 1,
    /** @brief The moon. */
    WorldShadowLight_Moon = 2,
} WorldShadowLight;

/**
 * @brief The sky's two directional lights at one moment, and which of them casts the shadows.
 *
 * A plain value, filled by WorldEnvironment_GetSkyLights from the date, the time and the settings. Intensities are
 * multipliers on the lights' colours (SunColor, MoonColor), with daylight, the moon's phase and height already in.
 */
typedef struct WorldSkyLightsStruct
{
    /** @brief Unit direction to the sun (WorldEnvironment_GetSunDirection). */
    Vector3 SunDirection;
    /** @brief How much daylight there is, 1 day .. 0 night (WorldEnvironment_GetDaylightFactor). */
    float Daylight;
    /** @brief The sun's light: SunIntensity x Daylight, >= 0. */
    float SunIntensity;
    /** @brief Unit direction to the moon (WorldEnvironment_GetMoonDirection). */
    Vector3 MoonDirection;
    /** @brief The moon's phase in [0, 1): 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter. */
    float MoonPhase;
    /** @brief The lit fraction of the moon's disc in [0, 1] (WorldEnvironment_GetMoonIllumination). */
    float MoonIllumination;
    /** @brief The moon's light: MoonIntensity x WorldEnvironment_GetMoonlightFactor, >= 0 (0 when the moon is off). */
    float MoonIntensity;
    /** @brief Which light casts the shadows: the brighter one (by luminance), once it stands high enough, or none. */
    WorldShadowLight ShadowLight;
    /** @brief Unit direction to the shadow light; the sun's direction when ShadowLight is None. */
    Vector3 ShadowDirection;
    /** @brief How strong the shadows are, 0..1: they fade in as the shadow light rises from 0.05 to 0.15 (the sine of
     *  its elevation), fade out while the two lights are about as bright (full once one is 4 times the other), so
     *  a hand-over from the sun to the moon (or back) never jumps, and fade out for a light too dim to show a shadow
     *  (a thin crescent). 0 when ShadowLight is None. */
    float ShadowFade;
} WorldSkyLights;

/**
 * @brief The environment/graphics settings of a single world.
 *
 * Populate with WorldEnvironment_SetDefaults, then adjust individual fields. Copy with WorldEnvironment_CopyTo.
 * Release with WorldEnvironment_Deconstruct (it frees the texture names).
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
     *  this angle). Sunrise and sunset stay on the horizon regardless. It also sets how high the sky's pole stands:
     *  it acts as the latitude. Finite; typically [0, PI/2). */
    float SunAngle;
    /** @brief When true, the sun follows the seasons: its declination swings 23.44 degrees north and south through
     *  the year (zero at the March equinox), so with SunAngle > 0 summer days are longer and the noon sun higher, and
     *  the moon follows the ecliptic too (a full moon stands low in summer, high in winter). Off by default: it changes
     *  how much daylight the working hours get. */
    bool AreSeasonsEnabled;

    /** @brief Atmospheric turbidity (haziness) for the physically based sky; higher is hazier: the haze's density is
     *  Earth's x turbidity / 3. Finite and >= 0. */
    float SkyTurbidity;
    /** @brief The haze's scale height, in metres: its density falls by e with every this many metres of height, so low
     *  haze hugs the ground and lets hills stand out above it (default 1200). Finite and in [10, 100000]. */
    float HazeHeight;
    /** @brief The haze's single-scattering albedo, sRGB: the share of the light it takes that it scatters, the rest it
     *  absorbs (default about 0.9 grey); darker or tinted for smog and dust. Alpha ignored. */
    Color HazeAlbedo;
    /** @brief How strongly the haze scatters light forward (the Mie anisotropy g): how large and bright the glow around
     *  the sun and the moon is, 0 none (the haze scatters evenly), 0.99 a tight, bright halo (default 0.758). Finite
     *  and in [0, 0.99]. */
    float SunGlow;
    /** @brief The air's Rayleigh scattering (what makes the sky blue) as a multiple of Earth's: thinner or denser air,
     *  for alien skies (default 1; SkyTint changes the colour). Finite and in [0, 100]. */
    float AirDensity;
    /** @brief The ozone layer's absorption as a multiple of Earth's: it takes orange and red from the light that
     *  crosses it, which keeps the twilight sky blue overhead (default 1; 0 none). Finite and in [0, 100]. */
    float Ozone;
    /** @brief How much of the light the air scatters more than once is drawn: 1 (the default) all of it, which
     *  brightens the horizon and fills the twilight sky, 0 single scattering only (for comparing). Finite and in
     *  [0, 1]. */
    float MultipleScattering;
    /** @brief The ground's colour as the atmosphere sees it, sRGB: the light it reflects up into the air feeds the
     *  sky's multiple scattering. Alpha ignored. */
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
    /** @brief Name of the texture asset the Milky Way is drawn from (default "sky_milky_way"; a resource pack can
     *  override that asset, a world can name another). Owned. A valid asset name of at most
     *  WORLD_ENVIRONMENT_TEXTURE_NAME_MAX_LENGTH bytes; never NULL. */
    unsigned char* MilkyWayTexture;
    /** @brief Light pollution from nearby towns, 0 (a dark site) to 1 (a city centre). It hides faint stars (at 1 only
     *  the brightest few remain), washes out the Milky Way, adds a glow near the horizon at night and brightens the
     *  night ambient a little. Finite and in [0, 1]. */
    float LightPollution;
    /** @brief Colour of the light-pollution glow (sodium orange by default). Alpha ignored. */
    Color LightPollutionColor;

    // ---- The moon ----
    /** @brief When false, the world has no moon: no disc, no moonlight, no moonlit sky. */
    bool IsMoonEnabled;
    /** @brief Where the moon's phase comes from: the date (the default) or MoonPhase. */
    WorldMoonPhaseSource MoonPhaseSource;
    /** @brief The phase used while MoonPhaseSource is Fixed: 0 new, 0.25 first quarter, 0.5 full (the default), 0.75
     *  last quarter; 1 is new again. Finite and in [0, 1]. */
    float MoonPhase;
    /** @brief Days added to the date before the calendar phase is taken: shifts the moon's cycle against the real one
     *  (about 7.4 days is a quarter phase). Finite. */
    float MoonPhaseOffsetDays;
    /** @brief Length of one cycle of phases, new moon to new moon, in days (default the real 29.530588). Finite and
     *  > 0. */
    float MoonCycleDays;
    /** @brief How far the moon's path strays north and south of the sun's, in radians (default 0.09, about the real
     *  moon's 5 degrees). Finite. */
    float MoonOrbitTilt;
    /** @brief The disc's angular size as a multiple of the real moon's 0.52 degrees (default 5, about 16 pixels wide
     *  at 640x360). Finite and > 0. */
    float MoonSizeMultiplier;
    /** @brief Brightness multiplier for the moon's disc in the sky (1 = natural); the moonlight is MoonIntensity.
     *  Finite and >= 0. */
    float MoonBrightness;
    /** @brief The moonlight of a full moon high in the sky, relative to the sun's light (the phase and the moon's
     *  height scale it, and the moonlit sky follows it). 0 gives a moon that lights nothing. Finite and >= 0. */
    float MoonIntensity;
    /** @brief Colour of the moonlight and the disc; blue-white gives the night look. Alpha ignored. */
    Color MoonColor;
    /** @brief Name of the texture asset the moon's face is drawn from (default "sky_moon"). Owned. A valid asset name
     *  of at most WORLD_ENVIRONMENT_TEXTURE_NAME_MAX_LENGTH bytes; never NULL. */
    unsigned char* MoonTexture;

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

    /** @brief How strongly the eye's night vision applies: in dim light colours fade into a faintly blue grey, as they
     *  do for the eye when the rods take over (a moonlit sky looks grey-blue, not as orange as a long-exposure photo
     *  shows it; bright things such as lamps, lit screens and the moon's disc keep their colour). 1 (the default) is the
     *  eye, 0 none: colourful nights. By day it changes nothing. Finite and in [0, 1]. */
    float NightVisionStrength;
    /** @brief How blue the night vision's grey is: 0 a neutral grey (the rods see no colour), 1 the deep night blue
     *  painters give night scenes (CIE x, y = 0.25, 0.25); 0.5 (the default) is half way, a faint blue cast. By day it
     *  changes nothing. Finite and in [0, 1]. */
    float NightVisionBlueShift;

    /** @brief How fast things fall in this world, in metres per second squared, straight down (-Y): characters (times
     *  their own gravity multiplier) and later physics objects. WORLD_ENVIRONMENT_DEFAULT_GRAVITY by default; a story on
     *  the moon sets 1.62. Finite and in [0, WORLD_ENVIRONMENT_MAX_GRAVITY]. */
    float Gravity;
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
 * Allocates the texture names. Call it on a new environment only and pair it with WorldEnvironment_Deconstruct; to
 * reset one, deconstruct it first (calling this again would leak the names it holds).
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
 * @brief Releases what an environment owns, its texture names (the table's Properties_Deconstruct; they become NULL).
 *
 * Safe to call on any constructed environment, also more than once, and on NULL.
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
 * points below. The east-west sweep is along the X axis (+X east). SunAngle tilts the arc out of vertical (around the
 * X axis, into Z), so at noon the sun sits SunAngle radians from the zenith rather than straight up (0 =
 * straight overhead). With AreSeasonsEnabled the arc also shifts with the date: north of the equator in summer, so
 * (for SunAngle > 0) the day is longer and the noon sun higher, south in winter; sunrise and sunset then leave the
 * exact east and west points. This is the light direction the sky/shadow/lighting passes use.
 * @param self The environment; must not be NULL.
 * @returns The normalized direction to the sun.
 */
Vector3 WorldEnvironment_GetSunDirection(const WorldEnvironment* self);

/**
 * @brief Returns the rotation from the celestial frame into the world for the current time of day.
 *
 * The celestial frame turns with the sun: the sun sits at its point (1,0,0) (with AreSeasonsEnabled at
 * (cos d, 0, -sin d) for the sun's declination d; this matrix applied to that equals WorldEnvironment_GetSunDirection),
 * and the daily rotation turns it around its z axis. SunAngle acts as the latitude: the celestial pole above the
 * horizon is -z (positive declinations lean toward it), which lies at world (0, sin SunAngle, -cos SunAngle) at every
 * time of day, so the sky turns around a fixed point that low over the horizon toward -Z. The moon is placed in this
 * frame; the stars are not (they drift against the sun through the year: WorldEnvironment_GetStarsToWorld).
 * The matrix is R_x(SunAngle) . R_z((TimeOfDay - 0.25) . 2pi), in raymath's convention (Vector3Transform applies it).
 * @param self The environment; must not be NULL.
 * @returns The celestial-to-world rotation (orthonormal, no translation).
 */
Matrix WorldEnvironment_GetCelestialToWorld(const WorldEnvironment* self);

/**
 * @brief Returns the rotation from the star frame into the world for the date and the time of day.
 *
 * The star frame is fixed among the stars: the renderer places the star catalogue and the Milky Way in it. It is the
 * celestial frame (WorldEnvironment_GetCelestialToWorld) of the moment the sun stands at right ascension 90 degrees
 * (the June solstice), turned about the pole by the sun's progress through the year since then: one extra turn per
 * year of 365.2422 days, continuous through the days and across New Year. So at the same time of day the stars stand
 * about 0.99 degrees further along their daily path each day (they rise about 4 minutes earlier), and the evening sky
 * of October differs from April's. The pole stays where WorldEnvironment_GetCelestialToWorld puts it.
 * @param self The environment; must not be NULL.
 * @returns The star-frame-to-world rotation (orthonormal, no translation).
 */
Matrix WorldEnvironment_GetStarsToWorld(const WorldEnvironment* self);

/**
 * @brief Returns the moon's phase in [0, 1): 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter.
 *
 * With MoonPhaseSource Calendar: frac((Date + TimeOfDay - WORLD_ENVIRONMENT_MOON_EPOCH_DAY + MoonPhaseOffsetDays) /
 * MoonCycleDays), computed in double, so with the defaults it is the real moon's phase for the date (within about a
 * day). With Fixed: MoonPhase (1 counts as 0, both are new moon). An unvalidated cycle length that is not a positive
 * finite number counts as the default; any other non-finite input gives 0.
 * @param self The environment; must not be NULL.
 * @returns The phase.
 */
float WorldEnvironment_GetMoonPhase(const WorldEnvironment* self);

/**
 * @brief Returns the unit direction pointing TO the moon.
 *
 * A "virtual" moon on the sun's path: east of the sun by the elongation 2pi x phase (so it trails the sun by that much
 * of a day: it rises about 50 minutes later each day of the cycle), and MoonOrbitTilt x sin(2pi x phase) north of it.
 * With AreSeasonsEnabled it follows the ecliptic too: its declination is the sun's declination of the moon's place in
 * the year plus that tilt term. So the new moon stands with the sun (with a tilt of 0, exactly), the full moon opposite,
 * the first quarter rises at noon and sets at midnight, the last quarter rises at midnight. Not an ephemeris: no
 * eclipses, no libration.
 * @param self The environment; must not be NULL.
 * @returns The normalized direction to the moon.
 */
Vector3 WorldEnvironment_GetMoonDirection(const WorldEnvironment* self);

/**
 * @brief Returns the lit fraction of the moon's disc: (1 - dot(sun, moon)) / 2, 0 at new moon, 0.5 at the quarters,
 *        1 at full moon.
 * @param self The environment; must not be NULL.
 * @returns The illumination in [0, 1].
 */
float WorldEnvironment_GetMoonIllumination(const WorldEnvironment* self);

/**
 * @brief Returns the moon's total light at an illumination, relative to the full moon's.
 *
 * Not linear: the opposition surge makes the full moon much brighter than its lit area suggests, and a half moon gives
 * about a tenth of the full moon's light. The fit of Krisciunas and Schaefer (1991, after Allen): the moon is
 * 0.026 |a| + 4e-9 a^4 magnitudes fainter than full at the phase angle a (degrees) = acos(2 x illumination - 1).
 * @param illumination The lit fraction, [0, 1] (clamped; NaN counts as 0).
 * @returns The brightness in [0, 1]: 1 at full moon, about 0.09 at a quarter, about 0.0003 at new moon.
 */
float WorldEnvironment_GetMoonBrightness(float illumination);

/**
 * @brief Returns how much of a full moon's light reaches the world right now, 0..1: the phase's brightness
 *        (WorldEnvironment_GetMoonBrightness), faded in from the horizon (full once the moon stands about 11 degrees
 *        high), and out by daylight (x (1 - WorldEnvironment_GetDaylightFactor)). 0 when IsMoonEnabled is false.
 *
 * The moonlight's intensity is MoonIntensity times this.
 * @param self The environment; must not be NULL.
 * @returns The moonlight factor in [0, 1].
 */
float WorldEnvironment_GetMoonlightFactor(const WorldEnvironment* self);

/**
 * @brief Computes the sky's two directional lights for the date and time, and which one casts the shadows.
 *
 * The sun's light is SunIntensity x daylight, the moon's MoonIntensity x WorldEnvironment_GetMoonlightFactor. The
 * shadow light is the brighter of the two by luminance (intensity x the luminance of its linear colour). Its shadows
 * fade in as it rises (none below 0.05, the sine of its elevation, full from 0.15) and fade out around the hand-over:
 * x smoothstep(0, ln 4, ln(brighter / dimmer)) for the two luminances, which is 0 where they are equal and 1 once one
 * is 4 times the other. So dusk with the moon up goes from sun shadows through no shadows to moon shadows without a
 * jump. A light dimmer than about a twentieth of the default full moon's casts none (its shadow could not show), and
 * neither light casts shadows when both are dark or below the horizon.
 * @param self The environment; must not be NULL.
 * @param outLights [out] Receives the lights; must not be NULL.
 */
void WorldEnvironment_GetSkyLights(const WorldEnvironment* self, WorldSkyLights* outLights);

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
