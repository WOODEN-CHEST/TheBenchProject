#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRJSON.h"


/**
 * @file Climate.h
 * @brief The climate asset: how a world's weather behaves over game time, as data a story creator writes.
 *
 * A climate is an asset of the "climate" type (JSON definitions in a resource pack's climates/ folder,
 * references/asset_structure.md; load one with AssetManager_LoadClimate). A world names its climate in its environment
 * (WorldEnvironment.Climate), so a town and a forest can differ; the weather system (WeatherSystem.h) runs it.
 *
 * THE WEATHER IS CONTINUOUS VALUES, never hard-coded states: the quantities below (WeatherQuantity) and two that follow
 * them (the surfaces' wetness, the wind's gusts). A climate's PRESETS ("clear", "storm", ...) are only targets for those
 * values, with a duration and weighted links to the presets that may follow; the weather system moves every value toward
 * its preset's target at its own rate. A preset may leave a value out: it then keeps moving toward its previous target.
 *
 * WHAT A CREATOR DECIDES (every key optional, the defaults are the reference's):
 *  - the mode (WeatherMode): simulated, fixed on one state, changed only by scripts, or off;
 *  - the seed (one number for the same weather in every playthrough, or a new one per save), the starting preset (or a
 *    random one) and an overall speed;
 *  - per preset: its targets (a number, or a [min, max] range drawn on every visit), its duration, its links and their
 *    weights, month weights (the seasons), the hours of the day it may start in, how long the move into it takes, its own
 *    rates, its gusts and its lightning;
 *  - the global rates, how fast surfaces get wet and dry, which clouds rain and which storm, and how much the forecast
 *    blurs.
 *
 * TIME. Durations, rates, the wetting and the drying count in GAME hours (the clock's, times the climate's speed), so
 * sleeping fast-forwards the weather. Gusts count in REAL seconds (the wind's speed is in metres per second of real
 * time, and a gust is something seen); lightning may be given per real minute, per game hour, or both.
 *
 * Every rule is checked when the definition is read (a broken file is skipped at startup, with a message naming the
 * path to the bad value): names, links and the starting and fixed presets must exist, weights must be above 0, month
 * weights are exactly twelve, hours lie in [0, 24], ranges are ordered, values lie in their quantity's range, and
 * unknown keys are refused everywhere.
 *
 * OWNERSHIP. The asset is plain data (no pointers inside, no dependencies); the asset manager owns it. Never keep its
 * pointer past your asset user (AssetManager.md).
 */


// Macros.
/** @brief The most presets a climate may hold. */
#define CLIMATE_MAX_PRESETS 64
/** @brief The most links (next presets) a climate may hold, over all its presets. */
#define CLIMATE_MAX_LINKS 512
/** @brief The longest preset name, in bytes, without the terminator. Names follow the asset names' rule
 *  ([a-z][a-z0-9_.-]*). */
#define CLIMATE_PRESET_NAME_MAX_LENGTH 64
/** @brief The months of a year: a preset's month weights hold one multiplier for each. */
#define CLIMATE_MONTH_COUNT 12
/** @brief The hours of a day: a preset's start hours lie in [0, this]. */
#define CLIMATE_HOURS_PER_DAY 24.0f
/** @brief The longest duration or transition a climate may give, in game hours (ten years; a bound for hostile
 *  content). */
#define CLIMATE_MAX_HOURS 87600.0f
/** @brief The shortest a preset may last, weather hours (36 seconds): it bounds the steps a fast-forward takes. */
#define CLIMATE_MIN_DURATION_HOURS 0.01f
/** @brief The largest rate of change a climate may give a quantity, in its unit per game hour (a bound for hostile
 *  content; far faster than any weather). */
#define CLIMATE_MAX_RATE 1.0e6f
/** @brief The fastest overall speed a climate may give its weather (times the game clock). */
#define CLIMATE_MAX_SPEED 10000.0f
/** @brief The most gusts or strikes a climate may ask for per real minute (a bound for hostile content). */
#define CLIMATE_MAX_PER_MINUTE 600.0f
/** @brief The most strikes a climate may ask for per game hour. */
#define CLIMATE_MAX_PER_HOUR 36000.0f
/** @brief The farthest a lightning strike may be, in metres. */
#define CLIMATE_MAX_STRIKE_DISTANCE 100000.0f
/** @brief The default range of the clouds' coverage x density over which rain becomes possible: none below its
 *  minimum, any amount above its maximum (a smoothstep between). */
#define CLIMATE_DEFAULT_RAIN_CLOUD_MIN 0.6f
#define CLIMATE_DEFAULT_RAIN_CLOUD_MAX 0.9f
/** @brief The default range of the clouds' type x density over which lightning becomes possible (storm clouds: tall and
 *  dense). */
#define CLIMATE_DEFAULT_STORM_CLOUD_MIN 0.5f
#define CLIMATE_DEFAULT_STORM_CLOUD_MAX 0.9f
/** @brief The default duration of a preset that names none, in game hours. */
#define CLIMATE_DEFAULT_DURATION_MIN_HOURS 2.0f
#define CLIMATE_DEFAULT_DURATION_MAX_HOURS 6.0f
/** @brief How fast surfaces get wet in a downpour, and dry, by default (per game hour; drying is faster in sun and wind). */
#define CLIMATE_DEFAULT_WETTING_PER_HOUR 1.5f
#define CLIMATE_DEFAULT_DRYING_PER_HOUR 0.15f


// Types.
/**
 * @brief The weather values a preset sets as targets and the weather system moves toward them (WorldEnvironment has a
 *        field for each). The order is the climate's and the weather system's index order, not saved anywhere: a
 *        new quantity may go anywhere.
 */
typedef enum WeatherQuantityEnum
{
    /** @brief cloud_coverage: how much of the sky has clouds, 0..1. */
    WeatherQuantity_CloudCoverage,
    /** @brief cloud_type: 0 thin, flat layers; 0.5 cumulus; 1 towering storm clouds. */
    WeatherQuantity_CloudType,
    /** @brief cloud_density: the clouds' optical thickness, 0..1: rain clouds are dense and dark. */
    WeatherQuantity_CloudDensity,
    /** @brief precipitation: rain, 0 none .. 1 a downpour. */
    WeatherQuantity_Precipitation,
    /** @brief wind_direction: where the wind blows toward, radians (degrees in JSON) around the vertical from +Z toward
     *  +X. Moves along the shorter way round. */
    WeatherQuantity_WindDirection,
    /** @brief wind_speed: the mean wind, metres per second. */
    WeatherQuantity_WindSpeed,
    /** @brief fog_density: ground fog, how much light it takes per metre at its base (on top of the world's ground
     *  haze). */
    WeatherQuantity_FogDensity,
    /** @brief fog_height: how quickly the ground fog thins upward, metres (by e every this many). */
    WeatherQuantity_FogHeight,
    /** @brief temperature: degrees Celsius. */
    WeatherQuantity_Temperature,
    /** @brief The number of quantities. */
    WeatherQuantity_Count,
} WeatherQuantity;

/**
 * @brief What one quantity is: its key, its range, its default rate and whether it is an angle. Static data.
 */
typedef struct WeatherQuantityInfoStruct
{
    /** @brief The key in JSON (the climate's and the environment's), snake_case. */
    const unsigned char* Key;
    /** @brief The smallest and the largest value it may take (an angle: any finite value, wrapped by the system). */
    float Minimum;
    float Maximum;
    /** @brief How fast it moves toward its target unless a climate says otherwise, in its unit per game hour. */
    float DefaultRatePerHour;
    /** @brief Whether it is an angle: radians in memory, degrees in JSON (ranges and rates too), moved along the shorter
     *  way round. */
    bool IsAngle;
} WeatherQuantityInfo;

/**
 * @brief How much of the weather the system runs. The values are saved (the weather system's mode, a climate's
 *        default): never renumber.
 */
typedef enum WeatherModeEnum
{
    /** @brief The simulation: presets follow each other by their links and the calendar's rules, values move toward
     *  their targets. */
    WeatherMode_Simulated = 0,
    /** @brief One permanent state: the climate's fixed preset or fixed values (or the active preset when it names
     *  neither). Wetness, gusts and lightning still follow the values. */
    WeatherMode_Fixed = 1,
    /** @brief Nothing changes unless a script says so: no preset follows another by itself; values move only toward
     *  targets a script set. Wetness, gusts and lightning still follow the values. */
    WeatherMode_Scripted = 2,
    /** @brief No weather system: the environment's weather values stay as they are (a world file's, as authored). */
    WeatherMode_Off = 3,
} WeatherMode;

/**
 * @brief A range of values, [Minimum, Maximum], Minimum <= Maximum; a single value has both equal. Drawn uniformly.
 */
typedef struct ClimateRangeStruct
{
    /** @brief The smallest value. */
    float Minimum;
    /** @brief The largest value. */
    float Maximum;
} ClimateRange;

/**
 * @brief One link of a preset: a preset that may follow it, and its weight among the preset's links.
 */
typedef struct ClimateLinkStruct
{
    /** @brief The preset that may follow, an index into the climate's presets. */
    uint16_t Preset;
    /** @brief Its weight, above 0: the chance of choosing it is weight x the month's weight of the linked preset, over
     *  the sum of the links allowed at the hour. */
    float Weight;
} ClimateLink;

/**
 * @brief A preset's gusts: how strong and how frequent the wind's gusts are while it is active.
 */
typedef struct ClimateGustsStruct
{
    /** @brief The gusts' strength on top of the mean wind, metres per second: a gust peaks between half of it and all of
     *  it. */
    float Strength;
    /** @brief How many gusts a minute of real time, on average (above 0). */
    float PerMinute;
} ClimateGusts;

/**
 * @brief A preset's lightning: how often it strikes (times how storm-like the clouds are) and how far away.
 */
typedef struct ClimateLightningStruct
{
    /** @brief Strikes per minute of real time, and per game hour (both count; either may be 0). */
    float PerMinute;
    float PerHour;
    /** @brief The nearest and the farthest a strike may be, metres (MinDistance <= MaxDistance); strikes are spread
     *  evenly over the area between, so most are far. */
    float MinDistance;
    float MaxDistance;
} ClimateLightning;

/**
 * @brief One preset: targets for the weather's values, how long it lasts and what may follow it. Plain data.
 */
typedef struct ClimatePresetStruct
{
    /** @brief The name ([a-z][a-z0-9_.-]*, at most CLIMATE_PRESET_NAME_MAX_LENGTH bytes), NUL-terminated. */
    unsigned char Name[CLIMATE_PRESET_NAME_MAX_LENGTH + 1];
    /** @brief Which quantities it sets: bit q for WeatherQuantity q. */
    uint32_t TargetMask;
    /** @brief The target of each quantity it sets, drawn from its range on every visit (radians for an angle). */
    ClimateRange Targets[WeatherQuantity_Count];
    /** @brief How long it lasts, game hours, drawn on every visit (Minimum above 0). */
    ClimateRange DurationHours;
    /** @brief Whether it has month weights, and the twelve multipliers (>= 0) on the chance of choosing it in each month
     *  of the calendar (January first): the seasons. */
    bool HasMonthWeights;
    float MonthWeights[CLIMATE_MONTH_COUNT];
    /** @brief Whether it may only start at some hours, and the window [Minimum, Maximum) in hours of the day (Minimum in
     *  [0, 24), Maximum in [0, 24], never equal); a Maximum below the Minimum wraps past midnight ([22, 4] is 22:00 to
     *  04:00). */
    bool HasStartHours;
    ClimateRange StartHours;
    /** @brief Whether the move into it takes a set time, and how many game hours (>= 0): every value it sets moves at
     *  the rate that brings it to its target then, so they all arrive together at the end (0: at once). */
    bool HasTransitionHours;
    float TransitionHours;
    /** @brief Which quantities it gives its own rate while it is active (bit q), and those rates (per game hour, >= 0;
     *  radians per hour for an angle). */
    uint32_t RateMask;
    float RatesPerHour[WeatherQuantity_Count];
    /** @brief Its gusts (no gusts without: strength 0). */
    bool HasGusts;
    ClimateGusts Gusts;
    /** @brief Its lightning (none without). */
    bool HasLightning;
    ClimateLightning Lightning;
    /** @brief Its links: Climate's links from FirstLink, LinkCount of them (0: it repeats itself when its time is up). */
    uint16_t FirstLink;
    uint16_t LinkCount;
} ClimatePreset;

/**
 * @brief A climate's settings outside its presets. Plain data.
 */
typedef struct ClimateSettingsStruct
{
    /** @brief The mode the weather starts in. */
    WeatherMode Mode;
    /** @brief Whether every new game draws its own seed ("per_save"), or Seed gives the same weather in every
     *  playthrough. */
    bool IsSeedPerSave;
    uint64_t Seed;
    /** @brief Whether the first preset is drawn (from every preset allowed at the start's month and hour), or
     *  StartPreset. */
    bool IsStartRandom;
    uint16_t StartPreset;
    /** @brief How fast the weather moves, times the game clock (above 0). */
    float Speed;
    /** @brief How fast each quantity moves toward its target, its unit per game hour (>= 0; an angle in radians). */
    float RatesPerHour[WeatherQuantity_Count];
    /** @brief How fast surfaces get wet in a downpour (times the precipitation), and how fast they dry (faster in sun
     *  and wind), per game hour (>= 0). */
    float WettingPerHour;
    float DryingPerHour;
    /** @brief How exact the forecast is, 0..1: 1 exact, less reports some future presets as one of their links. */
    float ForecastAccuracy;
    /** @brief The clouds' coverage x density over which rain becomes possible (none below Minimum, any amount above
     *  Maximum), and their type x density over which lightning does. Both in [0, 1]. */
    ClimateRange RainCloudRange;
    ClimateRange StormCloudRange;
    /** @brief Whether the fixed mode holds a preset, and which. */
    bool HasFixedPreset;
    uint16_t FixedPreset;
    /** @brief The values the fixed mode holds when it names no preset: bit q for each quantity given, and its value. */
    uint32_t FixedValueMask;
    float FixedValues[WeatherQuantity_Count];
} ClimateSettings;

/**
 * @brief The climate asset (opaque outside its module: read it through the functions below).
 */
typedef struct ClimateStruct Climate;


// Functions.
/**
 * @brief Returns what a quantity is: its key, range, default rate and whether it is an angle.
 * @param quantity The quantity.
 * @returns Its info (static); the first quantity's for a value outside the enum.
 */
const WeatherQuantityInfo* WeatherQuantity_GetInfo(WeatherQuantity quantity);

/**
 * @brief Finds the quantity of a JSON key ("cloud_coverage", ...).
 * @param key The key, NUL-terminated; NULL finds nothing.
 * @param outQuantity [out] Receives the quantity when found. Must not be NULL.
 * @returns Whether the key names a quantity.
 */
bool WeatherQuantity_TryParseKey(const unsigned char* key, WeatherQuantity* outQuantity);

/**
 * @brief Returns a mode's JSON name ("simulated", "fixed", "scripted", "off").
 * @param mode The mode.
 * @returns The name (static); "simulated" for a value outside the enum.
 */
const unsigned char* WeatherMode_GetName(WeatherMode mode);

/**
 * @brief Returns the climate's settings outside its presets.
 * @param self The climate. Must not be NULL.
 * @returns Its settings, valid as long as the climate.
 */
const ClimateSettings* Climate_GetSettings(const Climate* self);

/**
 * @brief Returns how many presets the climate has (at least 1).
 * @param self The climate. Must not be NULL.
 */
uint16_t Climate_GetPresetCount(const Climate* self);

/**
 * @brief Returns one of the climate's presets.
 * @param self The climate. Must not be NULL.
 * @param index The preset's index, below Climate_GetPresetCount.
 * @returns The preset, valid as long as the climate; NULL for an index out of range.
 */
const ClimatePreset* Climate_GetPreset(const Climate* self, uint16_t index);

/**
 * @brief Returns a preset's links (the presets that may follow it, with their weights).
 * @param self The climate. Must not be NULL.
 * @param preset The preset, one of the climate's. Must not be NULL.
 * @param outCount [out] Receives how many links it has. Must not be NULL.
 * @returns Its links, valid as long as the climate; NULL when it has none.
 */
const ClimateLink* Climate_GetLinks(const Climate* self, const ClimatePreset* preset, uint16_t* outCount);

/**
 * @brief Finds a preset by name.
 * @param self The climate. Must not be NULL.
 * @param name The name, NUL-terminated; NULL finds nothing.
 * @param outIndex [out] Receives its index when found. Must not be NULL.
 * @returns Whether the climate has a preset of that name.
 */
bool Climate_TryFindPreset(const Climate* self, const unsigned char* name, uint16_t* outIndex);

/**
 * @brief Reads a climate from a definition's JSON text, checking every rule, without the asset manager (the
 *        definition's parser uses it; tests and tools may too).
 *
 * The text is treated as hostile: unknown keys, a value of the wrong kind, a value out of its range, a missing preset, a
 * bad weight and a link past the limits are refused with a message naming where (`climate "default": presets[2]
 * ("storm").lightning.per_minute: ...`).
 * @param name The climate's name, for the messages; NUL-terminated, must not be NULL.
 * @param root The definition's root compound (its "name" key is the caller's, and skipped here); must not be NULL.
 * @param outClimate [out] Receives a new climate on success (free it with Climate_Destroy), NULL on failure. Must not be
 *        NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition for a broken rule or an unknown key; ErrorCode_InvalidJSON for a
 *          value of the wrong kind.
 */
Error Climate_ReadJSON(const unsigned char* name, JSONCompound* root, Climate** outClimate);

/**
 * @brief Copies a climate (one block, no pointers inside).
 * @param self The climate. Must not be NULL.
 * @returns A new copy; free it with Climate_Destroy.
 */
Climate* Climate_Clone(const Climate* self);

/**
 * @brief Frees a climate made by Climate_ReadJSON or Climate_Clone. Never call it on a climate the asset manager handed
 *        out. NULL does nothing.
 * @param self The climate.
 */
void Climate_Destroy(Climate* self);
