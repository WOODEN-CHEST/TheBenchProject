#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include "WeatherSystem.h"
#include "GameCalendar.h"
#include "wr/WRString.h"

/*
 * The weather system (agents/WeatherSystem.md). Two parts:
 *  - THE SIMULATION (WeatherSimulation and the functions on it: Simulate, Advance, ChooseNextPreset, EnterPreset): the
 *    deterministic game-time weather, pure over its state, the climate and the environment's sun, so the forecast runs a
 *    copy of it. Its steps end where a preset ends, so the presets chosen never depend on the step's length.
 *  - THE SYSTEM around it: the climate's asset, the start, the saved state's names, the gusts and the strikes in real
 *    time (their own random stream: the presets never depend on the frame rate), the strike queue and the log.
 */


// Macros.
/** Seconds of an hour and of a day. */
#define SECONDS_PER_HOUR 3600.0
#define SECONDS_PER_DAY 86400.0
/** A full turn, in radians. */
#define TWO_PI 6.283185307179586
/** The shortest a gust's rise or fall takes (real seconds), and the longest as a multiple of its mean. */
#define GUST_MIN_SECONDS 1.5f
#define GUST_MAX_MEAN_MULTIPLE 4.0f
/** A gust peaks between this share of the preset's strength and all of it; a lull falls to at most this share. */
#define GUST_PEAK_MIN_SHARE 0.5f
#define GUST_LULL_MAX_SHARE 0.2f
/** Without gusts the wind's gust dies down over this many real seconds. */
#define GUST_CALM_SECONDS 3.0f
/** The most gust segments one update may pass (a long real step): the rest of the step is skipped. */
#define GUST_MAX_SEGMENTS_PER_UPDATE 8
/** The flash: its time constant in real seconds (from 1 to under 2 % in 0.3 s), the most return strokes that follow a
 *  strike, how far apart they come (real seconds) and how bright they are; below FLASH_FLOOR it is out. */
#define FLASH_TIME_CONSTANT 0.075f
#define FLASH_RETURN_STROKE_MIN_SECONDS 0.04f
#define FLASH_RETURN_STROKE_MAX_SECONDS 0.12f
#define FLASH_RETURN_STROKE_MIN_PEAK 0.45f
#define FLASH_RETURN_STROKE_MAX_PEAK 0.9f
#define FLASH_FLOOR 1.0e-4f
/** A strike without lightning settings (WeatherSystem_Strike): its nearest and farthest distances, metres. */
#define DEFAULT_STRIKE_MIN_DISTANCE 300.0f
#define DEFAULT_STRIKE_MAX_DISTANCE 8000.0f
/** Drying: in full sun under a clear sky it is this many times faster on top of the base rate, and each this many metres
 *  per second of wind add the base rate once more. Rain stops it, fully from this much precipitation up. */
#define DRYING_SUN_FACTOR 2.0f
#define DRYING_WIND_REFERENCE 10.0f
#define DRYING_RAIN_STOP 0.1f
/** The forecast: its blur grows toward its full share (1 - accuracy) over about this many hours of lead; the longest and
 *  the most steps it runs. */
#define FORECAST_BLUR_HOURS 24.0
#define FORECAST_MAX_HOURS 8760.0
#define FORECAST_MAX_STEPS 10000.0
/** The salts the seed is mixed with for each random stream (splitmix64's constants: any odd 64-bit values would do). */
#define STREAM_PRESETS 0x9E3779B97F4A7C15ULL
#define STREAM_EVENTS 0xD1B54A32D192ED03ULL
#define STREAM_FORECAST 0x94D049BB133111EBULL
/** The weather system's property table's version. */
#define WEATHER_TABLE_VERSION 1U
/** The inspector's categories. */
#define CATEGORY_STATE ((const unsigned char*)u8"State")
#define CATEGORY_TARGETS ((const unsigned char*)u8"Targets")
#define CATEGORY_RATES ((const unsigned char*)u8"Rates")
#define CATEGORY_RANDOM ((const unsigned char*)u8"Random")
/** A saved row (written and read, not offered to the inspector or scripts: the API changes the weather). */
#define WEATHER_FLAGS_STATE ((uint32_t)PropertyFlags_Persistent)
/** The longest climate name the system keeps (the environment's limit). */
#define CLIMATE_NAME_MAX_LENGTH WORLD_ENVIRONMENT_CLIMATE_NAME_MAX_LENGTH


// Fields.
static const PropertyEnumEntry ModeEntries[] =
{
    { .Name = u8"simulated", .Value = WeatherMode_Simulated },
    { .Name = u8"fixed", .Value = WeatherMode_Fixed },
    { .Name = u8"scripted", .Value = WeatherMode_Scripted },
    { .Name = u8"off", .Value = WeatherMode_Off },
};

static const PropertyEnumTable ModeEnum =
{
    .Name = u8"weather_mode", .Entries = ModeEntries, .EntryCount = sizeof(ModeEntries) / sizeof(ModeEntries[0])
};

/* One saved float row: a target or a rate. */
#define FLOAT_ROW(key, wireId, member, type, rules, category, displayName) \
    { \
        .Key = key, .WireId = (wireId), .Type = (type), PROPERTY_MEMBER(WeatherSystem, member), \
        .Default.FloatValue = 0.0f, .Rules = rules, .Flags = WEATHER_FLAGS_STATE, .Category = (category), \
        .DisplayName = displayName, .Description = displayName, .SinceVersion = 1U \
    }
/* One saved word of a random stream. */
#define RANDOM_ROW(key, wireId, member) \
    { \
        .Key = key, .WireId = (wireId), .Type = PropertyType_UInt64, PROPERTY_MEMBER(WeatherSystem, member), \
        .Default.UInt64Value = 0U, .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_RANDOM, .DisplayName = key, \
        .Description = u8"A word of a random stream's state.", .SinceVersion = 1U \
    }
#define RATE_RULES PROPERTY_RULES_BETWEEN(0.0, CLIMATE_MAX_RATE)

static void WeatherAfterSet(void* instance, const PropertyDescriptor* descriptor);

/* The saved state. The wire ids are the save schema: never renumber or reuse one. */
static const PropertyDescriptor WeatherProperties[] =
{
    {
        .Key = u8"climate", .WireId = 1U, .Type = PropertyType_AssetName, PROPERTY_MEMBER(WeatherSystem, _climateName),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(CLIMATE_NAME_MAX_LENGTH, true),
        .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_STATE, .DisplayName = u8"Climate",
        .Description = u8"The climate the weather runs (none before it started).", .SinceVersion = 1U
    },
    {
        .Key = u8"preset", .WireId = 2U, .Type = PropertyType_AssetName, PROPERTY_MEMBER(WeatherSystem, _presetName),
        .Default.StringValue = NULL, .Rules = PROPERTY_RULES_STRING(CLIMATE_PRESET_NAME_MAX_LENGTH, true),
        .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_STATE, .DisplayName = u8"Preset",
        .Description = u8"The active preset.", .SinceVersion = 1U
    },
    {
        .Key = u8"is_started", .WireId = 3U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WeatherSystem, _isStarted),
        .Default.BoolValue = false, .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_STATE,
        .DisplayName = u8"Started", .Description = u8"Whether the weather started.", .SinceVersion = 1U
    },
    {
        .Key = u8"seed", .WireId = 4U, .Type = PropertyType_UInt64, PROPERTY_MEMBER(WeatherSystem, _seed),
        .Default.UInt64Value = 0U, .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_STATE, .DisplayName = u8"Seed",
        .Description = u8"The seed the random streams were made from.", .SinceVersion = 1U
    },
    {
        .Key = u8"mode", .WireId = 5U, .Type = PropertyType_Enum, PROPERTY_MEMBER(WeatherSystem, _simulation.Mode),
        .Default.EnumValue = WeatherMode_Simulated, .EnumTable = &ModeEnum, .Flags = WEATHER_FLAGS_STATE,
        .Category = CATEGORY_STATE, .DisplayName = u8"Mode",
        .Description = u8"Simulated, fixed, scripted or off.", .SinceVersion = 1U
    },
    {
        .Key = u8"is_locked", .WireId = 6U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WeatherSystem, _simulation.IsLocked),
        .Default.BoolValue = false, .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_STATE, .DisplayName = u8"Locked",
        .Description = u8"Whether the game-time weather is held.", .SinceVersion = 1U
    },
    {
        .Key = u8"speed", .WireId = 7U, .Type = PropertyType_Float, PROPERTY_MEMBER(WeatherSystem, _simulation.Speed),
        .Default.FloatValue = 1.0f, .Rules = { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0,
            .HasMaximum = true, .Maximum = CLIMATE_MAX_SPEED },
        .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_STATE, .DisplayName = u8"Speed",
        .Description = u8"How fast the weather moves, times the game clock.", .SinceVersion = 1U
    },
    {
        .Key = u8"preset_hours_left", .WireId = 8U, .Type = PropertyType_Double,
        PROPERTY_MEMBER(WeatherSystem, _simulation.PresetHoursLeft), .Default.DoubleValue = 0.0,
        .Rules = PROPERTY_RULES_BETWEEN(0.0, CLIMATE_MAX_HOURS), .Flags = WEATHER_FLAGS_STATE,
        .Category = CATEGORY_STATE, .DisplayName = u8"Preset hours left",
        .Description = u8"The weather hours left in the active preset.", .SinceVersion = 1U
    },
    {
        .Key = u8"preset_start_seconds", .WireId = 9U, .Type = PropertyType_Double,
        PROPERTY_MEMBER(WeatherSystem, _simulation.PresetStartSeconds), .Default.DoubleValue = 0.0,
        .Rules = PROPERTY_RULES_NONE, .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_STATE,
        .DisplayName = u8"Preset start", .Description = u8"When the active preset started (game seconds since day 0).",
        .SinceVersion = 1U
    },
    {
        .Key = u8"are_rates_pending", .WireId = 10U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WeatherSystem, _simulation.AreRatesPending), .Default.BoolValue = false,
        .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_RATES, .DisplayName = u8"Rates pending",
        .Description = u8"Whether the rates wait for the next update.", .SinceVersion = 1U
    },
    {
        .Key = u8"has_pending_transition", .WireId = 11U, .Type = PropertyType_Bool,
        PROPERTY_MEMBER(WeatherSystem, _simulation.HasPendingTransition), .Default.BoolValue = false,
        .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_RATES, .DisplayName = u8"Transition pending",
        .Description = u8"Whether the pending rates make a transition.", .SinceVersion = 1U
    },
    FLOAT_ROW(u8"pending_transition_hours", 12U, _simulation.PendingTransitionHours, PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, CLIMATE_MAX_HOURS), CATEGORY_RATES, u8"Pending transition hours"),
    FLOAT_ROW(u8"target_cloud_coverage", 13U, _simulation.Targets[WeatherQuantity_CloudCoverage], PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, 1.0), CATEGORY_TARGETS, u8"Cloud coverage's target"),
    FLOAT_ROW(u8"target_cloud_type", 14U, _simulation.Targets[WeatherQuantity_CloudType], PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, 1.0), CATEGORY_TARGETS, u8"Cloud type's target"),
    FLOAT_ROW(u8"target_cloud_density", 15U, _simulation.Targets[WeatherQuantity_CloudDensity], PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, 1.0), CATEGORY_TARGETS, u8"Cloud density's target"),
    FLOAT_ROW(u8"target_precipitation", 16U, _simulation.Targets[WeatherQuantity_Precipitation], PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, 1.0), CATEGORY_TARGETS, u8"Rain's target"),
    FLOAT_ROW(u8"target_wind_direction", 17U, _simulation.Targets[WeatherQuantity_WindDirection], PropertyType_Angle,
        PROPERTY_RULES_NONE, CATEGORY_TARGETS, u8"Wind direction's target"),
    FLOAT_ROW(u8"target_wind_speed", 18U, _simulation.Targets[WeatherQuantity_WindSpeed], PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_WIND_SPEED), CATEGORY_TARGETS, u8"Wind speed's target"),
    FLOAT_ROW(u8"target_fog_density", 19U, _simulation.Targets[WeatherQuantity_FogDensity], PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_GROUND_HAZE_DENSITY), CATEGORY_TARGETS, u8"Fog's target"),
    {
        .Key = u8"target_fog_height", .WireId = 20U, .Type = PropertyType_Float,
        PROPERTY_MEMBER(WeatherSystem, _simulation.Targets[WeatherQuantity_FogHeight]),
        .Default.FloatValue = (float)WORLD_ENVIRONMENT_MIN_GROUND_HAZE_THICKNESS,
        .Rules = PROPERTY_RULES_BETWEEN(WORLD_ENVIRONMENT_MIN_GROUND_HAZE_THICKNESS,
            WORLD_ENVIRONMENT_MAX_GROUND_HAZE_THICKNESS),
        .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_TARGETS, .DisplayName = u8"Fog height's target",
        .Description = u8"Fog height's target", .SinceVersion = 1U
    },
    FLOAT_ROW(u8"target_temperature", 21U, _simulation.Targets[WeatherQuantity_Temperature], PropertyType_Float,
        PROPERTY_RULES_BETWEEN(WORLD_ENVIRONMENT_MIN_TEMPERATURE, WORLD_ENVIRONMENT_MAX_TEMPERATURE), CATEGORY_TARGETS,
        u8"Temperature's target"),
    FLOAT_ROW(u8"rate_cloud_coverage", 22U, _simulation.Rates[WeatherQuantity_CloudCoverage], PropertyType_Float,
        RATE_RULES, CATEGORY_RATES, u8"Cloud coverage's rate"),
    FLOAT_ROW(u8"rate_cloud_type", 23U, _simulation.Rates[WeatherQuantity_CloudType], PropertyType_Float, RATE_RULES,
        CATEGORY_RATES, u8"Cloud type's rate"),
    FLOAT_ROW(u8"rate_cloud_density", 24U, _simulation.Rates[WeatherQuantity_CloudDensity], PropertyType_Float,
        RATE_RULES, CATEGORY_RATES, u8"Cloud density's rate"),
    FLOAT_ROW(u8"rate_precipitation", 25U, _simulation.Rates[WeatherQuantity_Precipitation], PropertyType_Float,
        RATE_RULES, CATEGORY_RATES, u8"Rain's rate"),
    FLOAT_ROW(u8"rate_wind_direction", 26U, _simulation.Rates[WeatherQuantity_WindDirection], PropertyType_Angle,
        RATE_RULES, CATEGORY_RATES, u8"Wind direction's rate"),
    FLOAT_ROW(u8"rate_wind_speed", 27U, _simulation.Rates[WeatherQuantity_WindSpeed], PropertyType_Float, RATE_RULES,
        CATEGORY_RATES, u8"Wind speed's rate"),
    FLOAT_ROW(u8"rate_fog_density", 28U, _simulation.Rates[WeatherQuantity_FogDensity], PropertyType_Float, RATE_RULES,
        CATEGORY_RATES, u8"Fog's rate"),
    FLOAT_ROW(u8"rate_fog_height", 29U, _simulation.Rates[WeatherQuantity_FogHeight], PropertyType_Float, RATE_RULES,
        CATEGORY_RATES, u8"Fog height's rate"),
    FLOAT_ROW(u8"rate_temperature", 30U, _simulation.Rates[WeatherQuantity_Temperature], PropertyType_Float,
        RATE_RULES, CATEGORY_RATES, u8"Temperature's rate"),
    RANDOM_ROW(u8"random_0", 31U, _simulation.Random.Values[0]),
    RANDOM_ROW(u8"random_1", 32U, _simulation.Random.Values[1]),
    RANDOM_ROW(u8"random_2", 33U, _simulation.Random.Values[2]),
    RANDOM_ROW(u8"random_3", 34U, _simulation.Random.Values[3]),
    RANDOM_ROW(u8"event_random_0", 35U, _eventRandom.Values[0]),
    RANDOM_ROW(u8"event_random_1", 36U, _eventRandom.Values[1]),
    RANDOM_ROW(u8"event_random_2", 37U, _eventRandom.Values[2]),
    RANDOM_ROW(u8"event_random_3", 38U, _eventRandom.Values[3]),
    FLOAT_ROW(u8"gust_from", 39U, _gustFrom, PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_WIND_SPEED), CATEGORY_STATE, u8"Gust from"),
    FLOAT_ROW(u8"gust_to", 40U, _gustTo, PropertyType_Float,
        PROPERTY_RULES_BETWEEN(0.0, WORLD_ENVIRONMENT_MAX_WIND_SPEED), CATEGORY_STATE, u8"Gust to"),
    FLOAT_ROW(u8"gust_seconds", 41U, _gustSeconds, PropertyType_Float, PROPERTY_RULES_AT_LEAST(0.0), CATEGORY_STATE,
        u8"Gust seconds"),
    FLOAT_ROW(u8"gust_duration", 42U, _gustDuration, PropertyType_Float, PROPERTY_RULES_AT_LEAST(0.0), CATEGORY_STATE,
        u8"Gust duration"),
    {
        .Key = u8"is_gust_rising", .WireId = 43U, .Type = PropertyType_Bool, PROPERTY_MEMBER(WeatherSystem, _isGustRising),
        .Default.BoolValue = false, .Flags = WEATHER_FLAGS_STATE, .Category = CATEGORY_STATE,
        .DisplayName = u8"Gust rising", .Description = u8"Whether the current gust heads to a peak.", .SinceVersion = 1U
    },
};

static const PropertyTable WeatherTable =
{
    .Name = u8"weather_system",
    .Version = WEATHER_TABLE_VERSION,
    .StructSize = sizeof(WeatherSystem),
    .Descriptors = WeatherProperties,
    .DescriptorCount = sizeof(WeatherProperties) / sizeof(WeatherProperties[0]),
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = WeatherAfterSet,
    .Migrate = NULL
};


// Static functions.
/* After a read or a set through the table: the next update finds the climate and the preset by name again. */
static void WeatherAfterSet(void* instance, const PropertyDescriptor* descriptor)
{
    (void)descriptor;
    WeatherSystem* Self = instance;
    Self->_isResolveNeeded = true;
}

/* Whether two NUL-terminated names are the same bytes (NULL equals only NULL). */
static bool AreNamesEqual(const unsigned char* a, const unsigned char* b)
{
    if ((a == NULL) || (b == NULL))
    {
        return a == b;
    }
    size_t Length = StringUTF8_GetByteLength(a);
    return (Length == StringUTF8_GetByteLength(b)) && Memory_IsEqual(a, b, Length);
}

/* A copy of a NUL-terminated name (NULL for NULL). */
static unsigned char* CloneName(const unsigned char* name)
{
    if (name == NULL)
    {
        return NULL;
    }
    size_t Length = StringUTF8_GetByteLength(name);
    unsigned char* Copy = Memory_Allocate(Length + 1U);
    Memory_Copy(name, Copy, Length);
    Copy[Length] = 0U;
    return Copy;
}

/* Replaces an owned name with a copy of another. */
static void ReplaceName(unsigned char** field, const unsigned char* name)
{
    unsigned char* Copy = CloneName(name);
    if (*field != NULL)
    {
        Memory_Free(*field);
    }
    *field = Copy;
}

/* splitmix64: the next value of a 64-bit counter's sequence, well mixed. */
static uint64_t SplitMix64(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ULL;
    uint64_t Value = *state;
    Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBULL;
    return Value ^ (Value >> 31);
}

/* A random stream's state made from a seed and the stream's salt. */
static RandomState MakeStream(uint64_t seed, uint64_t salt)
{
    uint64_t Counter = seed ^ salt;
    RandomState State;
    for (int Word = 0; Word < RANDOM_STATE_LENGTH; Word++)
    {
        State.Values[Word] = SplitMix64(&Counter);
    }
    return State;
}

/* A draw in [0, 1) from a stream, which advances. */
static double DrawUnit(RandomState* state)
{
    Random Generator;
    Random_Construct2(&Generator, *state);
    double Value = Random_NextDouble(&Generator);
    *state = Random_GetState(&Generator);
    Random_Deconstruct1(&Generator);
    return Value;
}

/* A draw from a range (its minimum when it is a single value). */
static float DrawFromRange(RandomState* state, ClimateRange range)
{
    double Unit = DrawUnit(state);
    return (float)((double)range.Minimum + ((double)range.Maximum - (double)range.Minimum) * Unit);
}

/* 0 below edge0, 1 above edge1, a smooth step between; a step at edge0 when the edges meet. */
static float SmoothStep(float edge0, float edge1, float x)
{
    if (!(edge1 > edge0))
    {
        return (x >= edge0) ? 1.0f : 0.0f;
    }
    float T = fminf(fmaxf((x - edge0) / (edge1 - edge0), 0.0f), 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

/* An angle wrapped into [-pi, pi). */
static double WrapSignedAngle(double angle)
{
    return angle - TWO_PI * floor((angle + 0.5 * TWO_PI) / TWO_PI);
}

/* An angle wrapped into [0, 2 pi). */
static float WrapAngle(double angle)
{
    double Wrapped = angle - TWO_PI * floor(angle / TWO_PI);
    float Value = (float)Wrapped;
    return ((Value >= 0.0f) && (Value < (float)TWO_PI)) ? Value : 0.0f;
}

/* The clock's time as game seconds since day 0. */
static double GetClockSeconds(const GameClock* clock)
{
    return (double)GameClock_GetDay(clock) * SECONDS_PER_DAY + GameClock_GetSecondsOfDay(clock);
}

/* Game seconds since day 0 as a day number and seconds of that day. */
static void SplitSeconds(double seconds, int64_t* outDay, double* outSecondsOfDay)
{
    double Day = floor(seconds / SECONDS_PER_DAY);
    double Rest = seconds - Day * SECONDS_PER_DAY;
    *outDay = (int64_t)Day;
    *outSecondsOfDay = (Rest < 0.0) ? 0.0 : ((Rest < SECONDS_PER_DAY) ? Rest : nextafter(SECONDS_PER_DAY, 0.0));
}

/* The month (0 January .. 11 December) and the hour of the day at a time. */
static int GetMonthIndex(double seconds)
{
    int64_t Day = 0;
    double Rest = 0.0;
    SplitSeconds(seconds, &Day, &Rest);
    int Month = (int)GameCalendar_GetDate(Day).Month - 1;
    return (Month < 0) ? 0 : ((Month >= CLIMATE_MONTH_COUNT) ? (CLIMATE_MONTH_COUNT - 1) : Month);
}

static float GetHourOfDay(double seconds)
{
    int64_t Day = 0;
    double Rest = 0.0;
    SplitSeconds(seconds, &Day, &Rest);
    return (float)(Rest / SECONDS_PER_HOUR);
}

/* Whether a preset may start at an hour of the day (its window wraps past midnight when it ends before it starts). */
static bool IsStartAllowed(const ClimatePreset* preset, float hour)
{
    if (!preset->HasStartHours)
    {
        return true;
    }
    float From = preset->StartHours.Minimum;
    float To = preset->StartHours.Maximum;
    return (From < To) ? ((hour >= From) && (hour < To)) : ((hour >= From) || (hour < To));
}

/* A preset's weight in a month (1 without month weights). */
static float GetMonthWeight(const ClimatePreset* preset, int month)
{
    return preset->HasMonthWeights ? preset->MonthWeights[month] : 1.0f;
}

/* The value's distance from its target: the shorter way round for an angle. */
static double GetDistanceToTarget(WeatherQuantity quantity, float value, float target)
{
    double Difference = (double)target - (double)value;
    return WeatherQuantity_GetInfo(quantity)->IsAngle ? fabs(WrapSignedAngle(Difference)) : fabs(Difference);
}

/* A value moved toward its target by at most maxStep (the shorter way round for an angle, wrapped into [0, 2 pi)). */
static float MoveToward(WeatherQuantity quantity, float value, float target, double maxStep)
{
    if (WeatherQuantity_GetInfo(quantity)->IsAngle)
    {
        double Difference = WrapSignedAngle((double)target - (double)value);
        double Step = fmax(fmin(Difference, maxStep), -maxStep);
        return (fabs(Difference) <= maxStep) ? WrapAngle((double)target) : WrapAngle((double)value + Step);
    }
    double Difference = (double)target - (double)value;
    if (fabs(Difference) <= maxStep)
    {
        return target;
    }
    return (float)((double)value + ((Difference > 0.0) ? maxStep : -maxStep));
}

/* Sets every quantity's rate for the active preset: its own rate or the climate's, or with a transition a rate that
 * brings each value it sets to its target after `transitionHours` (at once for 0). */
static void SetRates(WeatherSimulation* sim, const Climate* climate, const WeatherValues* values, bool hasTransition,
    float transitionHours)
{
    const ClimateSettings* Settings = Climate_GetSettings(climate);
    const ClimatePreset* Preset = Climate_GetPreset(climate, sim->Preset);
    // The fixed mode's explicit values: the quantities they leave out stay where they are.
    bool IsHoldingTheRest = (sim->Mode == WeatherMode_Fixed) && !Settings->HasFixedPreset
        && (Settings->FixedValueMask != 0U);
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        uint32_t Bit = (uint32_t)1U << Quantity;
        float Rate = ((Preset != NULL) && ((Preset->RateMask & Bit) != 0U)) ? Preset->RatesPerHour[Quantity]
            : Settings->RatesPerHour[Quantity];
        if (hasTransition && (Preset != NULL) && ((Preset->TargetMask & Bit) != 0U))
        {
            double Distance = GetDistanceToTarget((WeatherQuantity)Quantity, values->Quantities[Quantity],
                sim->Targets[Quantity]);
            Rate = (transitionHours > 0.0f) ? (float)fmin(Distance / (double)transitionHours, (double)CLIMATE_MAX_RATE)
                : CLIMATE_MAX_RATE;
        }
        if (IsHoldingTheRest && ((Settings->FixedValueMask & Bit) == 0U))
        {
            Rate = 0.0f;
        }
        sim->Rates[Quantity] = Rate;
    }
    sim->AreRatesPending = false;
    sim->HasPendingTransition = false;
    sim->PendingTransitionHours = 0.0f;
}

/* Enters a preset at a time: its targets and duration drawn; its rates set now from the values, or at the next update
 * (values NULL). */
static void EnterPreset(WeatherSimulation* sim, const Climate* climate, const WeatherValues* values, uint16_t index,
    bool hasTransition, float transitionHours, double nowSeconds)
{
    const ClimatePreset* Preset = Climate_GetPreset(climate, index);
    if (Preset == NULL)
    {
        return;
    }
    sim->Preset = index;
    sim->PresetStartSeconds = nowSeconds;
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        if ((Preset->TargetMask & ((uint32_t)1U << Quantity)) != 0U)
        {
            float Target = DrawFromRange(&sim->Random, Preset->Targets[Quantity]);
            sim->Targets[Quantity] = WeatherQuantity_GetInfo((WeatherQuantity)Quantity)->IsAngle ? WrapAngle(Target)
                : Target;
        }
    }
    sim->PresetHoursLeft = (double)DrawFromRange(&sim->Random, Preset->DurationHours);
    bool HasTransition = hasTransition || Preset->HasTransitionHours;
    float TransitionHours = hasTransition ? transitionHours : Preset->TransitionHours;
    if (values != NULL)
    {
        SetRates(sim, climate, values, HasTransition, TransitionHours);
        return;
    }
    sim->AreRatesPending = true;
    sim->HasPendingTransition = HasTransition;
    sim->PendingTransitionHours = HasTransition ? TransitionHours : 0.0f;
}

/* The preset that follows the active one at a time: by its links' weights x the month's weight of each, among those
 * allowed at the hour; the active one again when none is. */
static uint16_t ChooseNextPreset(WeatherSimulation* sim, const Climate* climate, double nowSeconds)
{
    const ClimatePreset* Current = Climate_GetPreset(climate, sim->Preset);
    if (Current == NULL)
    {
        return 0U;
    }
    uint16_t LinkCount = 0U;
    const ClimateLink* Links = Climate_GetLinks(climate, Current, &LinkCount);
    int Month = GetMonthIndex(nowSeconds);
    float Hour = GetHourOfDay(nowSeconds);
    double Total = 0.0;
    for (uint16_t Index = 0U; Index < LinkCount; Index++)
    {
        const ClimatePreset* Target = Climate_GetPreset(climate, Links[Index].Preset);
        if ((Target != NULL) && IsStartAllowed(Target, Hour))
        {
            Total += (double)Links[Index].Weight * (double)GetMonthWeight(Target, Month);
        }
    }
    if (!(Total > 0.0))
    {
        return sim->Preset;
    }
    double Pick = DrawUnit(&sim->Random) * Total;
    uint16_t Chosen = sim->Preset;
    for (uint16_t Index = 0U; Index < LinkCount; Index++)
    {
        const ClimatePreset* Target = Climate_GetPreset(climate, Links[Index].Preset);
        if ((Target == NULL) || !IsStartAllowed(Target, Hour))
        {
            continue;
        }
        double Weight = (double)Links[Index].Weight * (double)GetMonthWeight(Target, Month);
        if (!(Weight > 0.0))
        {
            continue;
        }
        Chosen = Links[Index].Preset;
        if (Pick < Weight)
        {
            break;
        }
        Pick -= Weight;
    }
    return Chosen;
}

/* The first preset of a climate that starts on a random one: by month weight among those allowed at the hour (all of
 * them by month weight when none is; the first when every weight is 0). */
static uint16_t ChooseRandomPreset(WeatherSimulation* sim, const Climate* climate, double nowSeconds)
{
    int Month = GetMonthIndex(nowSeconds);
    float Hour = GetHourOfDay(nowSeconds);
    uint16_t Count = Climate_GetPresetCount(climate);
    for (int Pass = 0; Pass < 2; Pass++)
    {
        double Total = 0.0;
        for (uint16_t Index = 0U; Index < Count; Index++)
        {
            const ClimatePreset* Preset = Climate_GetPreset(climate, Index);
            Total += ((Pass == 1) || IsStartAllowed(Preset, Hour)) ? (double)GetMonthWeight(Preset, Month) : 0.0;
        }
        if (!(Total > 0.0))
        {
            continue;
        }
        double Pick = DrawUnit(&sim->Random) * Total;
        uint16_t Chosen = 0U;
        for (uint16_t Index = 0U; Index < Count; Index++)
        {
            const ClimatePreset* Preset = Climate_GetPreset(climate, Index);
            double Weight = ((Pass == 1) || IsStartAllowed(Preset, Hour)) ? (double)GetMonthWeight(Preset, Month) : 0.0;
            if (!(Weight > 0.0))
            {
                continue;
            }
            Chosen = Index;
            if (Pick < Weight)
            {
                break;
            }
            Pick -= Weight;
        }
        return Chosen;
    }
    return 0U;
}

/* How much rain the clouds allow (the climate's rain cloud range over coverage x density). */
static float GetRainLimit(const ClimateSettings* settings, const WeatherValues* values)
{
    float Clouds = values->Quantities[WeatherQuantity_CloudCoverage] * values->Quantities[WeatherQuantity_CloudDensity];
    return SmoothStep(settings->RainCloudRange.Minimum, settings->RainCloudRange.Maximum, Clouds);
}

/* How storm-like the clouds are (the climate's storm cloud range over type x density). */
static float GetStorminess(const ClimateSettings* settings, const WeatherValues* values)
{
    float Clouds = values->Quantities[WeatherQuantity_CloudType] * values->Quantities[WeatherQuantity_CloudDensity];
    return SmoothStep(settings->StormCloudRange.Minimum, settings->StormCloudRange.Maximum, Clouds);
}

/* One step of `hours` weather hours ending at `endSeconds` (the clock's): every value toward its target (the clouds
 * first, then the rain within what they allow), then the wetness. */
static void Advance(WeatherSimulation* sim, const Climate* climate, const WorldEnvironment* environment,
    WeatherValues* values, double hours, double endSeconds)
{
    const ClimateSettings* Settings = Climate_GetSettings(climate);
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        if (Quantity == (int)WeatherQuantity_Precipitation)
        {
            continue;
        }
        values->Quantities[Quantity] = MoveToward((WeatherQuantity)Quantity, values->Quantities[Quantity],
            sim->Targets[Quantity], (double)sim->Rates[Quantity] * hours);
    }
    // Rain heads for no more than the clouds allow, at its own rate: it never rises past them, and when they thin
    // faster than it can ease off it follows them down at its rate.
    float RainBefore = values->Quantities[WeatherQuantity_Precipitation];
    float RainLimit = GetRainLimit(Settings, values);
    float Rain = MoveToward(WeatherQuantity_Precipitation, RainBefore,
        fminf(sim->Targets[WeatherQuantity_Precipitation], RainLimit),
        (double)sim->Rates[WeatherQuantity_Precipitation] * hours);
    values->Quantities[WeatherQuantity_Precipitation] = fminf(fmaxf(Rain, 0.0f), 1.0f);

    // Wetting with the rain; drying without it, faster in the sun (through the clouds) and the wind. The rain moves
    // linearly through a step, so its mean over the step is the mean of its ends (the trapezoid rule: a forecast's long
    // steps wet as many ticks do), and the sun is read at the step's middle.
    int64_t Day = 0;
    double Seconds = 0.0;
    double StepSeconds = hours * SECONDS_PER_HOUR / fmax((double)sim->Speed, 1.0e-9);
    SplitSeconds(endSeconds - 0.5 * StepSeconds, &Day, &Seconds);
    float Daylight = WorldEnvironment_GetDaylightFactorAt(environment, Day, (float)(Seconds / SECONDS_PER_DAY));
    float Clouds = values->Quantities[WeatherQuantity_CloudCoverage] * values->Quantities[WeatherQuantity_CloudDensity];
    float RainAfter = values->Quantities[WeatherQuantity_Precipitation];
    double MeanRain = 0.5 * ((double)RainBefore + (double)RainAfter);
    double DryingHeld = 0.5 * (double)(SmoothStep(0.0f, DRYING_RAIN_STOP, RainBefore)
        + SmoothStep(0.0f, DRYING_RAIN_STOP, RainAfter));
    double Drying = (double)Settings->DryingPerHour * (1.0 + (double)(DRYING_SUN_FACTOR * Daylight * (1.0f - Clouds)))
        * (1.0 + (double)values->Quantities[WeatherQuantity_WindSpeed] / (double)DRYING_WIND_REFERENCE)
        * (1.0 - DryingHeld);
    double Wetness = (double)values->Wetness + ((double)Settings->WettingPerHour * MeanRain - Drying) * hours;
    values->Wetness = (float)fmin(fmax(Wetness, 0.0), 1.0);
}

/* Runs the game-time weather for `clockSeconds` of the clock from `startSeconds` (its weather hours: times the speed), in
 * steps of at most WEATHER_SYSTEM_MAX_STEP_HOURS that end where a preset ends, through every preset it passes (each lasts
 * at least CLIMATE_MIN_DURATION_HOURS, so the steps are bounded by the time): however the time is split, the same presets
 * come at the same times. Returns how many presets it passed. */
static int Simulate(WeatherSimulation* sim, const Climate* climate, const WorldEnvironment* environment,
    WeatherValues* values, double startSeconds, double clockSeconds)
{
    if (sim->AreRatesPending)
    {
        SetRates(sim, climate, values, sim->HasPendingTransition, sim->PendingTransitionHours);
    }
    double Speed = (double)sim->Speed;
    if (!(Speed > 0.0) || !(clockSeconds > 0.0))
    {
        return 0;
    }
    double ClockSecondsPerHour = SECONDS_PER_HOUR / Speed;
    double HoursLeft = clockSeconds / ClockSecondsPerHour;
    double Now = startSeconds;
    bool IsTimed = sim->Mode == WeatherMode_Simulated;
    int Changes = 0;
    while (HoursLeft > 0.0)
    {
        if (IsTimed && (sim->PresetHoursLeft <= 0.0))
        {
            uint16_t Next = ChooseNextPreset(sim, climate, Now);
            EnterPreset(sim, climate, values, Next, false, 0.0f, Now);
            Changes++;
        }
        double Step = fmin(HoursLeft, WEATHER_SYSTEM_MAX_STEP_HOURS);
        if (IsTimed && (sim->PresetHoursLeft > 0.0))
        {
            Step = fmin(Step, sim->PresetHoursLeft);
        }
        double StepEnd = Now + Step * ClockSecondsPerHour;
        Advance(sim, climate, environment, values, Step, StepEnd);
        Now = StepEnd;
        HoursLeft -= Step;
        if (IsTimed)
        {
            sim->PresetHoursLeft = fmax(sim->PresetHoursLeft - Step, 0.0);
        }
    }
    return Changes;
}

/* Logs a line (nothing without a logger). */
static void LogLine(const WeatherSystem* self, const char* format, ...)
{
    if (self->_logger == NULL)
    {
        return;
    }
    char Message[512];
    va_list Arguments;
    va_start(Arguments, format);
    int Written = vsnprintf(Message, sizeof(Message), format, Arguments);
    va_end(Arguments);
    if (Written < 0)
    {
        return;
    }
    Error Result = Logger_LogInfo(self->_logger, (const unsigned char*)Message);
    Error_Deconstruct(&Result);
}

/* Logs the active preset and how long it lasts. */
static void LogPreset(const WeatherSystem* self, const char* what)
{
    const ClimatePreset* Preset = WeatherSystem_GetPreset(self);
    LogLine(self, "Weather: %s: \"%s\" for %.1f hours (%s%s).", what, (Preset != NULL) ? (const char*)Preset->Name : "?",
        self->_simulation.PresetHoursLeft, (const char*)WeatherMode_GetName(self->_simulation.Mode),
        self->_simulation.IsLocked ? ", locked" : "");
}

/* Keeps the saved preset name in step with the simulation's index (from the start on: a save made before the first
 * change names the first preset). */
static void UpdatePresetName(WeatherSystem* self)
{
    const ClimatePreset* Preset = (self->_climate != NULL) ? Climate_GetPreset(self->_climate, self->_simulation.Preset)
        : NULL;
    if ((Preset != NULL) && !AreNamesEqual(self->_presetName, Preset->Name))
    {
        ReplaceName(&self->_presetName, Preset->Name);
    }
}

/* Holds the fixed mode's state: the climate's fixed preset, its fixed values, or else the active preset's targets. */
static void EnterFixed(WeatherSystem* self, double nowSeconds)
{
    const Climate* Running = self->_climate;
    const ClimateSettings* Settings = Climate_GetSettings(Running);
    WeatherSimulation* Sim = &self->_simulation;
    if (Settings->HasFixedPreset)
    {
        EnterPreset(Sim, Running, NULL, Settings->FixedPreset, false, 0.0f, nowSeconds);
        UpdatePresetName(self);
        return;
    }
    if (Settings->FixedValueMask == 0U)
    {
        return;
    }
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        if ((Settings->FixedValueMask & ((uint32_t)1U << Quantity)) != 0U)
        {
            Sim->Targets[Quantity] = WeatherQuantity_GetInfo((WeatherQuantity)Quantity)->IsAngle
                ? WrapAngle((double)Settings->FixedValues[Quantity]) : Settings->FixedValues[Quantity];
        }
    }
    Sim->AreRatesPending = true;
    Sim->HasPendingTransition = false;
    Sim->PendingTransitionHours = 0.0f;
}

/* Enters the climate's first preset (or a random one) from the weather as it is: in the scripted mode it only gives its
 * gusts and lightning, and every value stays where it is until a script moves it; in the fixed mode the fixed state
 * follows. */
static void EnterFirstPreset(WeatherSystem* self, const WeatherValues* values, double nowSeconds)
{
    const ClimateSettings* Settings = Climate_GetSettings(self->_climate);
    WeatherSimulation* Sim = &self->_simulation;
    uint16_t First = Settings->IsStartRandom ? ChooseRandomPreset(Sim, self->_climate, nowSeconds) : Settings->StartPreset;
    if (Sim->Mode == WeatherMode_Scripted)
    {
        for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
        {
            Sim->Targets[Quantity] = values->Quantities[Quantity];
        }
        Sim->Preset = First;
        Sim->PresetStartSeconds = nowSeconds;
        Sim->PresetHoursLeft = 0.0;
        Sim->AreRatesPending = true;
        Sim->HasPendingTransition = false;
        Sim->PendingTransitionHours = 0.0f;
    }
    else
    {
        EnterPreset(Sim, self->_climate, NULL, First, false, 0.0f, nowSeconds);
    }
    if (Sim->Mode == WeatherMode_Fixed)
    {
        EnterFixed(self, nowSeconds);
    }
}

/* Makes both random streams from a seed. */
static void SeedStreams(WeatherSystem* self, uint64_t seed)
{
    self->_seed = seed;
    self->_simulation.Random = MakeStream(seed, STREAM_PRESETS);
    self->_eventRandom = MakeStream(seed, STREAM_EVENTS);
}

/* Starts the weather on its climate: the seed, the mode, the speed and the first preset (moved toward from the
 * environment's values: they are where the weather starts). */
static void Start(WeatherSystem* self, const WorldEnvironment* environment, const GameClock* clock)
{
    const ClimateSettings* Settings = Climate_GetSettings(self->_climate);
    uint64_t Seed = Settings->Seed;
    if (Settings->IsSeedPerSave)
    {
        Random Entropy;
        Random_Construct1(&Entropy);
        Seed = Random_NextValue(&Entropy);
        Random_Deconstruct1(&Entropy);
    }
    SeedStreams(self, Seed);
    WeatherSimulation* Sim = &self->_simulation;
    Sim->Mode = Settings->Mode;
    Sim->Speed = Settings->Speed;
    Sim->IsLocked = false;
    // A quantity no preset sets stays where the world starts it.
    WeatherValues Values = WeatherValues_FromEnvironment(environment);
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        Sim->Targets[Quantity] = Values.Quantities[Quantity];
    }
    EnterFirstPreset(self, &Values, GetClockSeconds(clock));
    self->_gustFrom = environment->WindGust;
    self->_gustTo = environment->WindGust;
    self->_gustSeconds = 0.0f;
    self->_gustDuration = 0.0f;
    self->_isGustRising = false;
    ReplaceName(&self->_climateName, self->_loadedClimateName);
    UpdatePresetName(self);
    self->_isStarted = true;
    self->_isResolveNeeded = false;
    LogLine(self, "Weather: the climate \"%s\" starts, seed %llu.", (const char*)self->_loadedClimateName,
        (unsigned long long)Seed);
    LogPreset(self, "now");
}

/* Moves a started weather onto a newly loaded climate: its mode, its speed and its starting preset (or a random one),
 * from the weather as it is. */
static void SwitchClimate(WeatherSystem* self, const WorldEnvironment* environment, const GameClock* clock)
{
    const ClimateSettings* Settings = Climate_GetSettings(self->_climate);
    WeatherSimulation* Sim = &self->_simulation;
    Sim->Mode = Settings->Mode;
    Sim->Speed = Settings->Speed;
    WeatherValues Values = WeatherValues_FromEnvironment(environment);
    EnterFirstPreset(self, &Values, GetClockSeconds(clock));
    ReplaceName(&self->_climateName, self->_loadedClimateName);
    UpdatePresetName(self);
    LogLine(self, "Weather: the climate \"%s\" takes over.", (const char*)self->_loadedClimateName);
    LogPreset(self, "now");
}

/* Finds the saved preset by name in the climate (after a read from a save); one it does not have restarts the climate's
 * first preset. */
static void Resolve(WeatherSystem* self, const WorldEnvironment* environment, const GameClock* clock)
{
    self->_isResolveNeeded = false;
    if (!AreNamesEqual(self->_climateName, self->_loadedClimateName))
    {
        SwitchClimate(self, environment, clock);
        return;
    }
    uint16_t Index = 0U;
    if (Climate_TryFindPreset(self->_climate, self->_presetName, &Index))
    {
        self->_simulation.Preset = Index;
        return;
    }
    LogLine(self, "Weather: the climate \"%s\" has no preset \"%s\" any more; it starts over.",
        (const char*)self->_loadedClimateName, (self->_presetName != NULL) ? (const char*)self->_presetName : "");
    SwitchClimate(self, environment, clock);
}

/* Loads the climate the environment names when it is new or changed; false while there is none (one that fails is
 * logged once, and not retried until the name changes). */
static bool EnsureClimate(WeatherSystem* self, const WorldEnvironment* environment, const GameClock* clock)
{
    const unsigned char* Wanted = environment->Climate;
    if (Wanted == NULL)
    {
        return false;
    }
    if ((self->_climate != NULL) && AreNamesEqual(self->_loadedClimateName, Wanted))
    {
        return true;
    }
    if (AreNamesEqual(self->_failedClimateName, Wanted))
    {
        return false;
    }
    // The old climate goes first: the user holds nothing else.
    Error ReleaseResult = AssetManager_ReleaseAllAssetsForUser(self->_assets, self->_user);
    Error_Deconstruct(&ReleaseResult);
    self->_climate = NULL;
    Climate* Loaded = NULL;
    Error Result = AssetManager_LoadClimate(self->_assets, Wanted, self->_user, &Loaded);
    if (Result.Code != ErrorCode_Success)
    {
        LogLine(self, "Weather: the climate \"%s\" could not be loaded (%s); the weather stays as it is.",
            (const char*)Wanted, (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
        Error_Deconstruct(&Result);
        ReplaceName(&self->_failedClimateName, Wanted);
        ReplaceName(&self->_loadedClimateName, NULL);
        return false;
    }
    self->_climate = Loaded;
    ReplaceName(&self->_loadedClimateName, Wanted);
    ReplaceName(&self->_failedClimateName, NULL);
    if (self->_isStarted && !self->_isResolveNeeded && !AreNamesEqual(self->_climateName, Wanted))
    {
        SwitchClimate(self, environment, clock);
    }
    return true;
}

/* The gusts in real time: the current segment eased (a smoothstep) from where it started to where it heads; at its end
 * the next one, alternately toward a peak and a lull, at the active preset's strength and frequency. */
static void UpdateGusts(WeatherSystem* self, WeatherValues* values, double realSeconds)
{
    if (!(realSeconds > 0.0))
    {
        return;
    }
    const ClimatePreset* Preset = WeatherSystem_GetPreset(self);
    float Strength = ((Preset != NULL) && Preset->HasGusts) ? Preset->Gusts.Strength : 0.0f;
    float PerMinute = ((Preset != NULL) && Preset->HasGusts) ? Preset->Gusts.PerMinute : 1.0f;
    self->_gustSeconds += (float)realSeconds;
    for (int Segment = 0; (Segment < GUST_MAX_SEGMENTS_PER_UPDATE) && (self->_gustSeconds >= self->_gustDuration);
        Segment++)
    {
        self->_gustSeconds -= self->_gustDuration;
        self->_gustFrom = self->_gustTo;
        if (!(Strength > 0.0f))
        {
            self->_gustTo = 0.0f;
            self->_gustDuration = GUST_CALM_SECONDS;
            self->_isGustRising = false;
            continue;
        }
        self->_isGustRising = !self->_isGustRising;
        float Unit = (float)DrawUnit(&self->_eventRandom);
        self->_gustTo = self->_isGustRising ? Strength * (GUST_PEAK_MIN_SHARE + (1.0f - GUST_PEAK_MIN_SHARE) * Unit)
            : Strength * GUST_LULL_MAX_SHARE * Unit;
        // A rise or a fall is half a gust: its mean is half the gusts' period; spread exponentially, within bounds.
        float Mean = 30.0f / fmaxf(PerMinute, 1.0e-3f);
        float Spread = (float)-log(1.0 - DrawUnit(&self->_eventRandom));
        self->_gustDuration = fminf(fmaxf(Mean * Spread, GUST_MIN_SECONDS), fmaxf(Mean * GUST_MAX_MEAN_MULTIPLE,
            GUST_MIN_SECONDS));
    }
    if (self->_gustSeconds >= self->_gustDuration)
    {
        self->_gustSeconds = 0.0f; // a step longer than the segments it allows: start the last one now
    }
    float Share = (self->_gustDuration > 0.0f) ? SmoothStep(0.0f, 1.0f, self->_gustSeconds / self->_gustDuration) : 1.0f;
    float Gust = self->_gustFrom + (self->_gustTo - self->_gustFrom) * Share;
    values->WindGust = fminf(fmaxf(Gust, 0.0f), (float)WORLD_ENVIRONMENT_MAX_WIND_SPEED);
}

/* Puts out the flash this system lit, whatever stopped the weather (the off mode however it was entered, a climate that
 * fails): one write to the environment, none while no flash of its own is lit. */
static void PutOutFlash(WeatherSystem* self, WorldEnvironment* environment)
{
    self->_flash = 0.0f;
    self->_returnStrokesLeft = 0;
    if (self->_isFlashLit)
    {
        environment->LightningFlash = 0.0f;
        self->_isFlashLit = false;
    }
}

/* A strike now: its direction and distance (spread evenly over the area between the distances, so most are far), the
 * flash at its peak with its return strokes to come, and the strike queued for the game. */
static void Strike(WeatherSystem* self, const GameClock* clock, float minDistance, float maxDistance)
{
    double Azimuth = TWO_PI * DrawUnit(&self->_eventRandom);
    self->_strikeDirection = (Vector3){ (float)sin(Azimuth), 0.0f, (float)cos(Azimuth) };
    double Near = (double)minDistance;
    double Far = (double)fmaxf(maxDistance, minDistance);
    self->_strikeDistance = (float)sqrt(Near * Near + (Far * Far - Near * Near) * DrawUnit(&self->_eventRandom));
    self->_flash = 1.0f;
    // Every stroke is drawn now, the unused ones too: the stream's place never depends on when the strokes come (or on
    // a save in between).
    self->_returnStrokesLeft = (int32_t)(DrawUnit(&self->_eventRandom) * (double)(WEATHER_SYSTEM_MAX_RETURN_STROKES + 1));
    for (int Stroke = 0; Stroke < WEATHER_SYSTEM_MAX_RETURN_STROKES; Stroke++)
    {
        self->_returnStrokeGaps[Stroke] = FLASH_RETURN_STROKE_MIN_SECONDS + (FLASH_RETURN_STROKE_MAX_SECONDS
            - FLASH_RETURN_STROKE_MIN_SECONDS) * (float)DrawUnit(&self->_eventRandom);
        self->_returnStrokePeaks[Stroke] = FLASH_RETURN_STROKE_MIN_PEAK + (FLASH_RETURN_STROKE_MAX_PEAK
            - FLASH_RETURN_STROKE_MIN_PEAK) * (float)DrawUnit(&self->_eventRandom);
    }
    self->_returnStroke = 0;
    self->_returnStrokeSeconds = self->_returnStrokeGaps[0];
    if (self->_strikes._count >= WEATHER_SYSTEM_MAX_QUEUED_STRIKES)
    {
        (void)GenericBuffer_RemoveFirst(&self->_strikes);
    }
    WeatherStrike Event = { .Day = GameClock_GetDay(clock), .SecondsOfDay = GameClock_GetSecondsOfDay(clock),
        .Direction = self->_strikeDirection, .Distance = self->_strikeDistance };
    (void)GenericBuffer_AddLast(&self->_strikes, &Event);
}

/* The flash in real time (it decays; its return strokes light it again), and new strikes: a Poisson process at the
 * preset's rates (per real minute and per weather hour) times how storm-like the clouds are, at most one an update. */
static void UpdateLightning(WeatherSystem* self, const WeatherValues* values, const GameClock* clock,
    double realSeconds, double weatherHours)
{
    if (realSeconds > 0.0)
    {
        self->_flash *= expf(-(float)realSeconds / FLASH_TIME_CONSTANT);
        if (self->_returnStrokesLeft > 0)
        {
            self->_returnStrokeSeconds -= (float)realSeconds;
            if ((self->_returnStrokeSeconds <= 0.0f) && (self->_returnStroke < WEATHER_SYSTEM_MAX_RETURN_STROKES))
            {
                self->_flash = fmaxf(self->_flash, self->_returnStrokePeaks[self->_returnStroke]);
                self->_returnStrokesLeft--;
                self->_returnStroke++;
                self->_returnStrokeSeconds = (self->_returnStroke < WEATHER_SYSTEM_MAX_RETURN_STROKES)
                    ? self->_returnStrokeGaps[self->_returnStroke] : 0.0f;
            }
        }
        if (self->_flash < FLASH_FLOOR)
        {
            self->_flash = 0.0f;
        }
    }
    const ClimatePreset* Preset = WeatherSystem_GetPreset(self);
    if ((Preset == NULL) || !Preset->HasLightning)
    {
        return;
    }
    double Expected = (double)GetStorminess(Climate_GetSettings(self->_climate), values)
        * ((double)Preset->Lightning.PerMinute * realSeconds / 60.0 + (double)Preset->Lightning.PerHour * weatherHours);
    if (!(Expected > 0.0))
    {
        return;
    }
    if (DrawUnit(&self->_eventRandom) < -expm1(-Expected))
    {
        Strike(self, clock, Preset->Lightning.MinDistance, Preset->Lightning.MaxDistance);
    }
}

/* A unit draw fixed by the seed and two numbers (the forecast's blur: the same visit always draws the same). */
static double HashUnit(uint64_t seed, uint64_t a, uint64_t b)
{
    uint64_t Counter = seed ^ STREAM_FORECAST ^ (a * 0xA24BAED4963EE407ULL) ^ (b * 0x9FB21C651E98DF25ULL);
    return (double)(SplitMix64(&Counter) >> 11) * (1.0 / 9007199254740992.0);
}

/* The preset a forecast reports for a visit: the visit's own, or with a share (1 - accuracy) growing with the lead time,
 * one of its links (by their weights), fixed per visit by the seed and the visit's start. */
static uint16_t ReportPreset(const WeatherSystem* self, const WeatherSimulation* sim, double nowSeconds,
    bool* outIsBlurred)
{
    *outIsBlurred = false;
    const Climate* Running = self->_climate;
    double Accuracy = (double)Climate_GetSettings(Running)->ForecastAccuracy;
    double LeadHours = (sim->PresetStartSeconds - nowSeconds) / SECONDS_PER_HOUR;
    const ClimatePreset* Preset = Climate_GetPreset(Running, sim->Preset);
    uint16_t LinkCount = 0U;
    const ClimateLink* Links = (Preset != NULL) ? Climate_GetLinks(Running, Preset, &LinkCount) : NULL;
    if (!(LeadHours > 0.0) || !(Accuracy < 1.0) || (LinkCount == 0U))
    {
        return sim->Preset;
    }
    uint64_t Visit = (uint64_t)(int64_t)llround(sim->PresetStartSeconds);
    double Blur = (1.0 - Accuracy) * -expm1(-LeadHours / FORECAST_BLUR_HOURS);
    if (!(HashUnit(self->_seed, Visit, sim->Preset) < Blur))
    {
        return sim->Preset;
    }
    double Total = 0.0;
    for (uint16_t Index = 0U; Index < LinkCount; Index++)
    {
        Total += (double)Links[Index].Weight;
    }
    double Pick = HashUnit(self->_seed, Visit, (uint64_t)sim->Preset + 0x10000ULL) * Total;
    uint16_t Chosen = Links[LinkCount - 1U].Preset;
    for (uint16_t Index = 0U; Index < LinkCount; Index++)
    {
        if (Pick < (double)Links[Index].Weight)
        {
            Chosen = Links[Index].Preset;
            break;
        }
        Pick -= (double)Links[Index].Weight;
    }
    *outIsBlurred = Chosen != sim->Preset;
    return Chosen;
}


// Public functions.
const PropertyTable* WeatherSystem_GetPropertyTable(void)
{
    return &WeatherTable;
}

Error WeatherSystem_Construct(WeatherSystem* self, AssetManager* assets, Logger* logger)
{
    AssetUserID User = ASSET_USER_ID_INVALID;
    Error Result = AssetManager_GetNewUserID(assets, &User);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    Properties_Construct(&WeatherTable, self);
    self->_assets = assets;
    self->_logger = logger;
    self->_user = User;
    self->_climate = NULL;
    self->_loadedClimateName = NULL;
    self->_failedClimateName = NULL;
    self->_isResolveNeeded = false;
    self->_nowSeconds = 0.0;
    self->_simulation.Preset = 0U;
    self->_flash = 0.0f;
    self->_returnStrokesLeft = 0;
    self->_returnStrokeSeconds = 0.0f;
    self->_returnStroke = 0;
    Memory_Zero(self->_returnStrokeGaps, sizeof(self->_returnStrokeGaps));
    Memory_Zero(self->_returnStrokePeaks, sizeof(self->_returnStrokePeaks));
    self->_isFlashLit = false;
    self->_strikeDirection = (Vector3){ 0.0f, 0.0f, 1.0f };
    self->_strikeDistance = DEFAULT_STRIKE_MIN_DISTANCE;
    GenericBuffer_AllocateVariable(&self->_strikes, WEATHER_SYSTEM_MAX_QUEUED_STRIKES, sizeof(WeatherStrike));
    return Error_CreateSuccess();
}

void WeatherSystem_Deconstruct(WeatherSystem* self)
{
    if (self == NULL)
    {
        return;
    }
    if (self->_assets != NULL)
    {
        Error Result = AssetManager_RetireUser(self->_assets, self->_user);
        Error_Deconstruct(&Result);
    }
    self->_climate = NULL;
    ReplaceName(&self->_loadedClimateName, NULL);
    ReplaceName(&self->_failedClimateName, NULL);
    Properties_Deconstruct(&WeatherTable, self);
    Memory_Free(self->_strikes._data);
    self->_strikes._data = NULL;
    self->_assets = NULL;
}

WeatherValues WeatherValues_FromEnvironment(const WorldEnvironment* environment)
{
    WeatherValues Values;
    Values.Quantities[WeatherQuantity_CloudCoverage] = environment->CloudCoverage;
    Values.Quantities[WeatherQuantity_CloudType] = environment->CloudType;
    Values.Quantities[WeatherQuantity_CloudDensity] = environment->CloudDensity;
    Values.Quantities[WeatherQuantity_Precipitation] = environment->Precipitation;
    Values.Quantities[WeatherQuantity_WindDirection] = environment->WindDirection;
    Values.Quantities[WeatherQuantity_WindSpeed] = environment->WindSpeed;
    Values.Quantities[WeatherQuantity_FogDensity] = environment->FogDensity;
    Values.Quantities[WeatherQuantity_FogHeight] = environment->FogHeight;
    Values.Quantities[WeatherQuantity_Temperature] = environment->Temperature;
    Values.Wetness = environment->Wetness;
    Values.WindGust = environment->WindGust;
    return Values;
}

void WeatherValues_ToEnvironment(const WeatherValues* self, WorldEnvironment* environment)
{
    environment->CloudCoverage = self->Quantities[WeatherQuantity_CloudCoverage];
    environment->CloudType = self->Quantities[WeatherQuantity_CloudType];
    environment->CloudDensity = self->Quantities[WeatherQuantity_CloudDensity];
    environment->Precipitation = self->Quantities[WeatherQuantity_Precipitation];
    environment->WindDirection = self->Quantities[WeatherQuantity_WindDirection];
    environment->WindSpeed = self->Quantities[WeatherQuantity_WindSpeed];
    environment->FogDensity = self->Quantities[WeatherQuantity_FogDensity];
    environment->FogHeight = self->Quantities[WeatherQuantity_FogHeight];
    environment->Temperature = self->Quantities[WeatherQuantity_Temperature];
    environment->Wetness = self->Wetness;
    environment->WindGust = self->WindGust;
}

const ClimatePreset* WeatherSystem_GetPreset(const WeatherSystem* self)
{
    return (self->_isStarted && (self->_climate != NULL) && !self->_isResolveNeeded)
        ? Climate_GetPreset(self->_climate, self->_simulation.Preset) : NULL;
}

void WeatherSystem_Update(WeatherSystem* self, WorldEnvironment* environment, const GameClock* clock,
    double gameSeconds, double realSeconds)
{
    double GameSeconds = (isfinite(gameSeconds) && (gameSeconds > 0.0)) ? gameSeconds : 0.0;
    double RealSeconds = (isfinite(realSeconds) && (realSeconds > 0.0)) ? realSeconds : 0.0;
    self->_nowSeconds = GetClockSeconds(clock);
    if (!EnsureClimate(self, environment, clock))
    {
        PutOutFlash(self, environment);
        return;
    }
    if (!self->_isStarted)
    {
        Start(self, environment, clock);
    }
    else if (self->_isResolveNeeded)
    {
        Resolve(self, environment, clock);
    }
    WeatherSimulation* Sim = &self->_simulation;
    if (Sim->Mode == WeatherMode_Off)
    {
        PutOutFlash(self, environment);
        return;
    }

    WeatherValues Values = WeatherValues_FromEnvironment(environment);
    double WeatherHours = 0.0;
    if (!Sim->IsLocked && (GameSeconds > 0.0))
    {
        uint16_t Before = Sim->Preset;
        double End = GetClockSeconds(clock);
        int Changes = Simulate(Sim, self->_climate, environment, &Values, End - GameSeconds, GameSeconds);
        WeatherHours = GameSeconds * (double)Sim->Speed / SECONDS_PER_HOUR;
        if ((Changes > 0) || (Sim->Preset != Before))
        {
            UpdatePresetName(self);
            LogPreset(self, "next");
        }
    }
    UpdateGusts(self, &Values, RealSeconds);
    UpdateLightning(self, &Values, clock, RealSeconds, WeatherHours);
    WeatherValues_ToEnvironment(&Values, environment);
    environment->LightningFlash = self->_flash;
    self->_isFlashLit = self->_flash > 0.0f;
    environment->LightningDirection = self->_strikeDirection;
    environment->LightningDistance = self->_strikeDistance;
}

Error WeatherSystem_SetMode(WeatherSystem* self, WeatherMode mode)
{
    if (((int)mode < (int)WeatherMode_Simulated) || ((int)mode > (int)WeatherMode_Off))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange, "WeatherSystem_SetMode: no such mode.");
    }
    if (!self->_isStarted || (self->_climate == NULL) || self->_isResolveNeeded)
    {
        return Error_Construct2(ErrorCode_InvalidState, "WeatherSystem_SetMode: the weather has not started.");
    }
    WeatherSimulation* Sim = &self->_simulation;
    if (Sim->Mode == mode)
    {
        return Error_CreateSuccess();
    }
    // Leaving the fixed mode gives the quantities it held their rates back (the next update).
    Sim->AreRatesPending = Sim->AreRatesPending || (Sim->Mode == WeatherMode_Fixed);
    Sim->Mode = mode;
    if (mode == WeatherMode_Fixed)
    {
        EnterFixed(self, self->_nowSeconds);
    }
    LogPreset(self, "mode");
    return Error_CreateSuccess();
}

Error WeatherSystem_SetPreset(WeatherSystem* self, const unsigned char* name, float transitionHours)
{
    if (!isfinite(transitionHours) || (transitionHours < 0.0f) || (transitionHours > CLIMATE_MAX_HOURS))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "WeatherSystem_SetPreset: the transition (%g hours) must be finite and in [0, %g].", (double)transitionHours,
            (double)CLIMATE_MAX_HOURS);
    }
    if (!self->_isStarted || (self->_climate == NULL) || self->_isResolveNeeded)
    {
        return Error_Construct2(ErrorCode_InvalidState, "WeatherSystem_SetPreset: the weather has not started.");
    }
    uint16_t Index = 0U;
    if (!Climate_TryFindPreset(self->_climate, name, &Index))
    {
        return Error_Construct4(ErrorCode_InvalidOperation, "WeatherSystem_SetPreset: the climate \"%s\" has no preset "
            "\"%s\".", (const char*)self->_loadedClimateName, (name != NULL) ? (const char*)name : "");
    }
    EnterPreset(&self->_simulation, self->_climate, NULL, Index, true, transitionHours, self->_nowSeconds);
    UpdatePresetName(self);
    LogPreset(self, "set");
    return Error_CreateSuccess();
}

Error WeatherSystem_SkipPreset(WeatherSystem* self, const GameClock* clock)
{
    if (!self->_isStarted || (self->_climate == NULL) || self->_isResolveNeeded)
    {
        return Error_Construct2(ErrorCode_InvalidState, "WeatherSystem_SkipPreset: the weather has not started.");
    }
    double Now = GetClockSeconds(clock);
    uint16_t Next = ChooseNextPreset(&self->_simulation, self->_climate, Now);
    EnterPreset(&self->_simulation, self->_climate, NULL, Next, false, 0.0f, Now);
    UpdatePresetName(self);
    LogPreset(self, "next (skipped)");
    return Error_CreateSuccess();
}

Error WeatherSystem_SetValuesNow(WeatherSystem* self, WorldEnvironment* environment, uint32_t mask,
    const float values[WeatherQuantity_Count])
{
    if (!self->_isStarted)
    {
        return Error_Construct2(ErrorCode_InvalidState, "WeatherSystem_SetValuesNow: the weather has not started.");
    }
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        if ((mask & ((uint32_t)1U << Quantity)) == 0U)
        {
            continue;
        }
        const WeatherQuantityInfo* Info = WeatherQuantity_GetInfo((WeatherQuantity)Quantity);
        float Value = values[Quantity];
        if (!isfinite(Value) || (Value < Info->Minimum) || (Value > Info->Maximum))
        {
            return Error_Construct4(ErrorCode_ArgumentOutOfRange,
                "WeatherSystem_SetValuesNow: %s (%g) must be finite and in [%g, %g].", (const char*)Info->Key,
                (double)Value, (double)Info->Minimum, (double)Info->Maximum);
        }
    }
    WeatherValues Values = WeatherValues_FromEnvironment(environment);
    for (int Quantity = 0; Quantity < (int)WeatherQuantity_Count; Quantity++)
    {
        if ((mask & ((uint32_t)1U << Quantity)) == 0U)
        {
            continue;
        }
        float Value = WeatherQuantity_GetInfo((WeatherQuantity)Quantity)->IsAngle ? WrapAngle((double)values[Quantity])
            : values[Quantity];
        Values.Quantities[Quantity] = Value;
        self->_simulation.Targets[Quantity] = Value;
    }
    WeatherValues_ToEnvironment(&Values, environment);
    return Error_CreateSuccess();
}

void WeatherSystem_SetLocked(WeatherSystem* self, bool isLocked)
{
    if (self->_simulation.IsLocked == isLocked)
    {
        return;
    }
    self->_simulation.IsLocked = isLocked;
    LogPreset(self, isLocked ? "locked" : "unlocked");
}

void WeatherSystem_SetSeed(WeatherSystem* self, uint64_t seed)
{
    SeedStreams(self, seed);
}

Error WeatherSystem_SetSpeed(WeatherSystem* self, float speed)
{
    if (!isfinite(speed) || !(speed > 0.0f) || (speed > CLIMATE_MAX_SPEED))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "WeatherSystem_SetSpeed: the speed (%g) must be finite and in (0, %g].", (double)speed,
            (double)CLIMATE_MAX_SPEED);
    }
    self->_simulation.Speed = speed;
    return Error_CreateSuccess();
}

Error WeatherSystem_SetClimate(WeatherSystem* self, WorldEnvironment* environment, const unsigned char* name)
{
    (void)self;
    PropertyValue Value = PropertyValue_FromAssetName(name);
    return Properties_Set(WorldEnvironment_GetPropertyTable(), environment, u8"climate", &Value);
}

void WeatherSystem_Strike(WeatherSystem* self, const GameClock* clock)
{
    if (self->_simulation.Mode == WeatherMode_Off)
    {
        return;
    }
    const ClimatePreset* Preset = WeatherSystem_GetPreset(self);
    bool HasLightning = (Preset != NULL) && Preset->HasLightning;
    Strike(self, clock, HasLightning ? Preset->Lightning.MinDistance : DEFAULT_STRIKE_MIN_DISTANCE,
        HasLightning ? Preset->Lightning.MaxDistance : DEFAULT_STRIKE_MAX_DISTANCE);
}

const WeatherStrike* WeatherSystem_GetStrikes(const WeatherSystem* self, size_t* outCount)
{
    *outCount = self->_strikes._count;
    return (self->_strikes._count > 0U) ? (const WeatherStrike*)self->_strikes._data : NULL;
}

void WeatherSystem_ClearStrikes(WeatherSystem* self)
{
    (void)GenericBuffer_Clear(&self->_strikes);
}

Error WeatherSystem_Forecast(const WeatherSystem* self, const WorldEnvironment* environment, const GameClock* clock,
    double hoursAhead, double stepHours, GenericBuffer* destination)
{
    if (!isfinite(hoursAhead) || !(hoursAhead > 0.0) || (hoursAhead > FORECAST_MAX_HOURS) || !isfinite(stepHours)
        || !(stepHours > 0.0) || ((hoursAhead / stepHours) > FORECAST_MAX_STEPS))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WeatherSystem_Forecast: %g hours ahead in steps of %g "
            "hours: the hours must be in (0, %g] and take at most %g steps.", hoursAhead, stepHours, FORECAST_MAX_HOURS,
            FORECAST_MAX_STEPS);
    }
    if (!self->_isStarted || (self->_climate == NULL) || self->_isResolveNeeded)
    {
        return Error_Construct2(ErrorCode_InvalidState, "WeatherSystem_Forecast: the weather has not started.");
    }
    if ((hoursAhead * (double)self->_simulation.Speed) > FORECAST_MAX_HOURS)
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange, "WeatherSystem_Forecast: %g hours ahead at %g times the "
            "speed is more than %g of the weather's hours.", hoursAhead, (double)self->_simulation.Speed,
            FORECAST_MAX_HOURS);
    }
    WeatherSimulation Sim = self->_simulation;
    WeatherValues Values = WeatherValues_FromEnvironment(environment);
    double Now = GetClockSeconds(clock);
    double Time = Now;
    int StepCount = (int)ceil(hoursAhead / stepHours);
    for (int Step = 1; Step <= StepCount; Step++)
    {
        double Hours = fmin(stepHours, hoursAhead - (double)(Step - 1) * stepHours);
        if ((Sim.Mode != WeatherMode_Off) && !Sim.IsLocked)
        {
            Simulate(&Sim, self->_climate, environment, &Values, Time, Hours * SECONDS_PER_HOUR);
        }
        Time += Hours * SECONDS_PER_HOUR;
        WeatherForecastStep Entry;
        Memory_Zero(&Entry, sizeof(Entry));
        SplitSeconds(Time, &Entry.Day, &Entry.SecondsOfDay);
        Entry.Preset = ReportPreset(self, &Sim, Now, &Entry.IsBlurred);
        Entry.Values = Values;
        if (!GenericBuffer_AddLast(destination, &Entry))
        {
            return Error_Construct2(ErrorCode_BufferTooSmall, "WeatherSystem_Forecast: the destination is full.");
        }
    }
    return Error_CreateSuccess();
}
