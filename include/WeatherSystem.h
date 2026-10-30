#pragma once
#include <stdbool.h>
#include <stdint.h>
// Vector3 is raylib's: a strike's direction. WorldEnvironment.h includes it too.
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRRandom.h"
#include "AssetManager.h"
#include "Climate.h"
#include "GameClock.h"
#include "Logger.h"
#include "PropertyDescriptor.h"
#include "WorldEnvironment.h"


/**
 * @file WeatherSystem.h
 * @brief The weather: continuous values moved over game time by a seeded simulation of a world's climate.
 *
 * Game logic, headless. The system reads the climate a world's environment names (WorldEnvironment.Climate, Climate.h)
 * and writes the weather's values into that environment every tick, where the renderer reads them: cloud coverage, type
 * and density, precipitation, wetness, the wind and its gusts, the ground fog, the temperature, and the lightning's flash.
 *
 * EACH TICK (WeatherSystem_Update), after the game clock moved:
 *  - THE PRESET'S TIME. In the simulated mode, when the active preset's time is up the next is chosen from its links,
 *    by weight x the month's weight of each, among those whose start hours allow the hour; its targets (a range is drawn
 *    on every visit) and its duration are drawn. Nothing allowed: the preset stays for another visit.
 *  - THE VALUES move toward their targets at their rates (the move into a preset with a transition time takes exactly
 *    that long); rain heads for no more than the clouds allow (rain only from dense clouds: the climate's rain cloud
 *    range over coverage x density), so it never rises past them and eases off at its rate when they thin; wetness
 *    rises with the rain and dries afterwards (faster in sun and wind).
 *    All of it runs in game hours times the speed, in steps split where a preset ends, so a fast-forward (sleeping)
 *    moves the weather through every preset it passes, and a forecast with other steps chooses the same presets.
 *  - THE GUSTS (a smooth random rise and fall of WindGust at the preset's strength and frequency) and THE LIGHTNING (a
 *    Poisson process at the preset's rate times how storm-like the clouds are; a strike sets the flash to 1, which decays
 *    over a fraction of a real second with a few return strokes, and queues a WeatherStrike for the game's thunder) run
 *    in REAL seconds of play, so they look the same at any clock speed; strikes given per game hour count game time too.
 *
 * MODES (WeatherMode, the climate's to start with): simulated; fixed (the climate's fixed preset or values, else the
 * active preset, held for good); scripted (nothing changes unless the story calls the API below); off (the system does
 * not touch the environment). Wetness, gusts and lightning follow the values in every mode but off. LOCKING holds the
 * game-time weather (values, the preset's time, wetness) for a scene; gusts and lightning go on.
 *
 * SAVING. Everything that decides what comes next is in the system's property table (WeatherSystem_GetPropertyTable):
 * the climate's and the active preset's names, the mode, the lock, the speed, the seed, both random streams, the targets
 * and rates, the gusts' state. Read it back with the table's readers, then keep updating: the next update finds the
 * climate and the preset by name again, and the simulation goes on as if it had never stopped. The flash and queued
 * strikes are not saved.
 *
 * OWNERSHIP. The system holds its climate under its own asset user (AssetManager.md): construct it with
 * WeatherSystem_Construct and release it with WeatherSystem_Deconstruct. The environment, the clock and the logger are
 * borrowed for each call. Not thread-safe.
 */


// Macros.
/** @brief How many weather hours one simulation step covers at most (a fast-forward takes many). */
#define WEATHER_SYSTEM_MAX_STEP_HOURS 0.05
/** @brief The most return strokes a strike has: each lights the flash again a little later. */
#define WEATHER_SYSTEM_MAX_RETURN_STROKES 3
/** @brief The speed of sound in air, metres per real second: a strike's thunder arrives its distance / this after the
 *  flash. */
#define WEATHER_SYSTEM_SPEED_OF_SOUND 343.0f


// Types.
/**
 * @brief One lightning strike, queued for the game (thunder, a reaction). A plain value.
 */
typedef struct WeatherStrikeStruct
{
    /** @brief The game date and time of the strike (the clock's day number and seconds of the day). */
    int64_t Day;
    double SecondsOfDay;
    /** @brief The level direction of the strike from the viewer (unit, Y 0). */
    Vector3 Direction;
    /** @brief How far away it struck, metres: its thunder arrives Distance / WEATHER_SYSTEM_SPEED_OF_SOUND real seconds
     *  after the flash. */
    float Distance;
} WeatherStrike;

/**
 * @brief The weather's values at one moment, as the system moves them (the environment's weather fields). A plain
 *        value.
 */
typedef struct WeatherValuesStruct
{
    /** @brief Each quantity's value (WeatherQuantity order; the wind's direction in radians). */
    float Quantities[WeatherQuantity_Count];
    /** @brief How wet surfaces are, 0..1. */
    float Wetness;
    /** @brief The current gust on top of the mean wind, metres per second. */
    float WindGust;
} WeatherValues;

/**
 * @brief One step of a forecast (WeatherSystem_Forecast). A plain value.
 */
typedef struct WeatherForecastStepStruct
{
    /** @brief The game date and time the step is for. */
    int64_t Day;
    double SecondsOfDay;
    /** @brief The preset the forecast reports for then, an index into the climate (Climate_GetPreset). */
    uint16_t Preset;
    /** @brief Whether the forecast reports another preset than the simulation will have (its accuracy below 1). */
    bool IsBlurred;
    /** @brief The values the simulation will have then. */
    WeatherValues Values;
} WeatherForecastStep;

/**
 * @brief The deterministic part of the weather's state: what decides what comes next. Plain data, copied by value (a
 *        forecast runs a copy). Read only outside the system.
 */
typedef struct WeatherSimulationStruct
{
    /** @brief The mode, whether the game-time weather is held, and the speed (times the game clock, above 0). */
    WeatherMode Mode;
    bool IsLocked;
    float Speed;
    /** @brief The active preset (an index into the climate; valid once started and resolved), the weather hours left in
     *  it, and when it started (the clock's seconds since day 0: the forecast's blur is drawn per visit by it). */
    uint16_t Preset;
    double PresetHoursLeft;
    double PresetStartSeconds;
    /** @brief Each quantity's target and its rate (its unit per weather hour; radians for the wind's direction). */
    float Targets[WeatherQuantity_Count];
    float Rates[WeatherQuantity_Count];
    /** @brief Whether the rates wait for the next update's values (a preset or mode set from the API), whether they then
     *  make a transition (every value the preset sets arriving at once, after PendingTransitionHours of weather time;
     *  >= 0) or take the preset's and the climate's own rates. */
    bool AreRatesPending;
    bool HasPendingTransition;
    float PendingTransitionHours;
    /** @brief The presets' random stream (the choices, the drawn targets and durations). */
    RandomState Random;
} WeatherSimulation;

/**
 * @brief The weather system. Construct with WeatherSystem_Construct; fields are read only outside its module (the
 *        property table writes the saved ones).
 */
typedef struct WeatherSystemStruct
{
    // ---- Saved (the property table's rows) ----
    /** @brief The climate the system runs (owned; NULL before it started). */
    unsigned char* _climateName;
    /** @brief The active preset's name (owned; NULL before it started). */
    unsigned char* _presetName;
    /** @brief Whether it started (chose its seed, mode and first preset). */
    bool _isStarted;
    /** @brief The seed the random streams were made from. */
    uint64_t _seed;
    /** @brief The deterministic state. */
    WeatherSimulation _simulation;
    /** @brief The gusts' and the strikes' random stream (real time; apart from the presets', so the presets never depend
     *  on the frame rate). */
    RandomState _eventRandom;
    /** @brief The current gust's segment: from where and to where it moves, the real seconds into it and its length,
     *  and whether it heads to a peak. */
    float _gustFrom;
    float _gustTo;
    float _gustSeconds;
    float _gustDuration;
    bool _isGustRising;

    // ---- Runtime ----
    /** @brief Borrowed: the asset manager the climate loads from, and the logger (NULL logs nothing). */
    AssetManager* _assets;
    Logger* _logger;
    /** @brief The system's own asset user (it holds its climate). */
    AssetUserID _user;
    /** @brief The climate held (NULL: none), and the name it was loaded by. */
    const Climate* _climate;
    unsigned char* _loadedClimateName;
    /** @brief A climate name that failed to load (owned; logged once, not retried until the name changes). */
    unsigned char* _failedClimateName;
    /** @brief Whether the climate and the active preset still need finding by name (after a read from a save). */
    bool _isResolveNeeded;
    /** @brief The clock's time at the last update, game seconds since day 0: when a preset or a mode set from the API
     *  starts. */
    double _nowSeconds;
    /** @brief The flash, 0..1 (written to the environment), the return strokes still to come, the real seconds until
     *  the next, and every stroke's gap and peak, drawn at the strike (so the random stream never waits on them). */
    float _flash;
    int32_t _returnStrokesLeft;
    float _returnStrokeSeconds;
    int32_t _returnStroke;
    float _returnStrokeGaps[WEATHER_SYSTEM_MAX_RETURN_STROKES];
    float _returnStrokePeaks[WEATHER_SYSTEM_MAX_RETURN_STROKES];
    /** @brief Whether the environment holds a flash this system wrote that is not out yet: whatever stops the weather
     *  (the off mode, a climate that fails) puts it out with one write. */
    bool _isFlashLit;
    /** @brief The latest strike's direction and distance (written to the environment). */
    Vector3 _strikeDirection;
    float _strikeDistance;
    /** @brief Queued strikes (WeatherStrike) since the game last cleared them; at most WEATHER_SYSTEM_MAX_QUEUED_STRIKES. */
    GenericBuffer _strikes;
} WeatherSystem;

/** @brief The most strikes the queue holds; older ones are dropped while the game does not clear it. */
#define WEATHER_SYSTEM_MAX_QUEUED_STRIKES 64


// Functions.
/**
 * @brief Returns the weather system's property table: the saved state's rows (table "weather_system").
 *
 * Save the system with the table's writers (Properties_WriteJSON / Properties_WriteGHDF on the system itself) and load it
 * with its readers; after a read the next update finds the climate and the preset by name. Properties_Set on any row
 * does the same.
 * @returns The table; static, never NULL.
 */
const PropertyTable* WeatherSystem_GetPropertyTable(void);

/**
 * @brief Constructs a weather system that has not started: the first update loads the climate the environment names and
 *        starts it.
 * @param self The system to construct. Must not be NULL.
 * @param assets The asset manager the climate loads from (borrowed; must outlive the system). Must not be NULL.
 * @param logger The logger for the weather's changes (borrowed); NULL logs nothing.
 * @returns Success; the error from minting the system's asset user (then nothing was constructed).
 */
Error WeatherSystem_Construct(WeatherSystem* self, AssetManager* assets, Logger* logger);

/**
 * @brief Releases the system: its climate (its asset user is retired), its names and its queue.
 * @param self The system, or NULL.
 */
void WeatherSystem_Deconstruct(WeatherSystem* self);

/**
 * @brief Moves the weather by one tick and writes its values into the environment.
 *
 * Loads the climate the environment names when it is new or changed (one that fails is logged once and the weather
 * stays as it is until the name changes), starts the system on its first update (the seed, the mode, the speed, the first
 * preset; the environment's values are where the weather starts), then moves it: the game-time weather by
 * @p gameSeconds x the speed (unless locked), the gusts, the strikes and the flash by @p realSeconds. In the off mode it
 * touches nothing. Never logs per tick: only the climate's start, a preset's change (at most one line per update) and
 * failures.
 * @param self The system. Must not be NULL.
 * @param environment The world's environment: read for its climate's name, its weather values and its sun; written
 *        with the new values. Must not be NULL.
 * @param clock The game clock, already advanced by this tick (its date and hour drive the seasons and start hours; its
 *        time is when the moved time ends). Must not be NULL.
 * @param gameSeconds The game seconds the clock moved (GameClockCrossings.GameSeconds, a fast-forward's too); finite and
 *        >= 0, anything else counts as 0.
 * @param realSeconds The real seconds of play this tick (0 while the game is paused or skips time); finite and >= 0,
 *        anything else counts as 0.
 */
void WeatherSystem_Update(WeatherSystem* self, WorldEnvironment* environment, const GameClock* clock,
    double gameSeconds, double realSeconds);

/**
 * @brief Switches the mode. Into fixed: the climate's fixed preset or fixed values (what they leave out stays where it
 *        is), else the active preset, held from the next update. Into off: the next update puts out a flash the system
 *        lit, and nothing is touched after. Logged.
 * @param self The system. Must not be NULL.
 * @param mode The mode.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a mode outside the enum; ErrorCode_InvalidState before the system
 *          started, or after a read until the next update found the saved preset.
 */
Error WeatherSystem_SetMode(WeatherSystem* self, WeatherMode mode);

/**
 * @brief Moves the weather into a preset (in any mode: in the simulated mode it then runs from it), its targets and
 *        duration drawn, every value it sets arriving after @p transitionHours of weather time (0: at the next update).
 * @param self The system. Must not be NULL.
 * @param name The preset's name in the running climate. Must not be NULL.
 * @param transitionHours How long the move takes, in weather hours; finite and >= 0.
 * @returns Success; ErrorCode_ArgumentOutOfRange for a bad duration; ErrorCode_InvalidOperation for a name the climate
 *          does not have; ErrorCode_InvalidState before the system started, or after a read until the next update found
 *          the saved preset.
 */
Error WeatherSystem_SetPreset(WeatherSystem* self, const unsigned char* name, float transitionHours);

/**
 * @brief Moves the simulation into the active preset's next one now (chosen as when its time is up). For tools and debug
 *        keys.
 * @param self The system. Must not be NULL.
 * @param clock The game clock (its month and hour choose). Must not be NULL.
 * @returns Success; ErrorCode_InvalidState before the system started, or after a read until the next update found the
 *          saved preset.
 */
Error WeatherSystem_SkipPreset(WeatherSystem* self, const GameClock* clock);

/**
 * @brief Sets some of the weather's values at once, in the environment and as their targets (they stay until a preset
 *        or a script moves them).
 * @param self The system. Must not be NULL.
 * @param environment The world's environment, written. Must not be NULL.
 * @param mask Which quantities to set: bit q for WeatherQuantity q.
 * @param values Every quantity's value (only the masked ones are read; the wind's direction in radians). Must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange naming the first value out of its quantity's range (nothing is set);
 *          ErrorCode_InvalidState before the system started.
 */
Error WeatherSystem_SetValuesNow(WeatherSystem* self, WorldEnvironment* environment, uint32_t mask,
    const float values[WeatherQuantity_Count]);

/**
 * @brief Holds the game-time weather (values, the preset's time, wetness) for a scripted scene, or lets it go on. Gusts
 *        and lightning go on either way. Logged.
 * @param self The system. Must not be NULL.
 * @param isLocked Whether to hold it.
 */
void WeatherSystem_SetLocked(WeatherSystem* self, bool isLocked);

/**
 * @brief Sets the seed and makes both random streams from it again (the same seed then gives the same weather from here).
 * @param self The system. Must not be NULL.
 * @param seed The seed.
 */
void WeatherSystem_SetSeed(WeatherSystem* self, uint64_t seed);

/**
 * @brief Sets how fast the weather moves, times the game clock.
 * @param self The system. Must not be NULL.
 * @param speed The speed; finite, above 0 and at most CLIMATE_MAX_SPEED.
 * @returns Success; ErrorCode_ArgumentOutOfRange otherwise (nothing changes).
 */
Error WeatherSystem_SetSpeed(WeatherSystem* self, float speed);

/**
 * @brief Switches the world to another climate: writes its name into the environment; the next update loads it, takes
 *        its mode and speed and moves into its starting preset from the weather as it is.
 * @param self The system. Must not be NULL.
 * @param environment The world's environment. Must not be NULL.
 * @param name The climate's asset name. Must not be NULL.
 * @returns Success; the table's error for a name that is not a valid asset name.
 */
Error WeatherSystem_SetClimate(WeatherSystem* self, WorldEnvironment* environment, const unsigned char* name);

/**
 * @brief A strike now, at the active preset's distances (300 to 8000 m without lightning settings): the flash and a
 *        queued WeatherStrike. In the off mode nothing happens. For scripts and debug keys.
 * @param self The system. Must not be NULL.
 * @param clock The game clock (the strike's time). Must not be NULL.
 */
void WeatherSystem_Strike(WeatherSystem* self, const GameClock* clock);

/**
 * @brief Returns the strikes queued since the game last cleared them (WeatherSystem_ClearStrikes), oldest first.
 * @param self The system. Must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The strikes, valid until the next update or clear; NULL when there are none.
 */
const WeatherStrike* WeatherSystem_GetStrikes(const WeatherSystem* self, size_t* outCount);

/**
 * @brief Empties the strike queue (the game calls it once it handled the strikes).
 * @param self The system. Must not be NULL.
 */
void WeatherSystem_ClearStrikes(WeatherSystem* self);

/**
 * @brief Runs a copy of the simulation ahead and appends what it expects: a step every @p stepHours of game time up to
 *        @p hoursAhead.
 *
 * Exact (the simulation then does the same, unless a script steps in) at the climate's forecast accuracy 1; below it,
 * some presets of later visits are reported as one of their links: more of them the further ahead (a share of
 * 1 - accuracy, growing toward it over a day), each visit's choice drawn from the seed and the visit's start, so the same
 * forecast is given every time it is asked, and a visit is reported right once it starts. Gusts and lightning are not
 * forecast (Values.WindGust stays as it is). Allocates nothing but the buffer's growth.
 * @param self The system. Must not be NULL.
 * @param environment The world's environment (the values now, the sun's daylight ahead for the drying). Must not be NULL.
 * @param clock The game clock (now). Must not be NULL.
 * @param hoursAhead How far ahead, in game hours; finite, above 0, at most 8760 (a year), and at most a year of the
 *        weather's own hours (hoursAhead x the speed <= 8760).
 * @param stepHours The step, in game hours; finite and above 0; at most 10000 steps.
 * @param destination A buffer of WeatherForecastStep the steps are appended to (not cleared first). Must not be NULL.
 * @returns Success; ErrorCode_ArgumentOutOfRange for bad hours; ErrorCode_InvalidState before the system started, while
 *          its climate is not loaded, or after a read until the next update found the saved preset.
 */
Error WeatherSystem_Forecast(const WeatherSystem* self, const WorldEnvironment* environment, const GameClock* clock,
    double hoursAhead, double stepHours, GenericBuffer* destination);

/**
 * @brief Reads the environment's weather values.
 * @param environment The environment. Must not be NULL.
 * @returns Its values.
 */
WeatherValues WeatherValues_FromEnvironment(const WorldEnvironment* environment);

/**
 * @brief Writes weather values into the environment's weather fields (the lightning's are left alone).
 * @param self The values. Must not be NULL.
 * @param environment The environment. Must not be NULL.
 */
void WeatherValues_ToEnvironment(const WeatherValues* self, WorldEnvironment* environment);

/**
 * @brief Returns the running climate (NULL before it loaded, or while it fails to). Valid until the next update.
 * @param self The system. Must not be NULL.
 */
static inline const Climate* WeatherSystem_GetClimate(const WeatherSystem* self)
{
    return self->_climate;
}

/**
 * @brief Returns the active preset (NULL before the system started or while its climate is not loaded).
 * @param self The system. Must not be NULL.
 */
const ClimatePreset* WeatherSystem_GetPreset(const WeatherSystem* self);

/**
 * @brief Returns the deterministic state: the mode, the lock, the speed, the preset's hours left, targets and rates.
 * @param self The system. Must not be NULL.
 */
static inline const WeatherSimulation* WeatherSystem_GetSimulation(const WeatherSystem* self)
{
    return &self->_simulation;
}

/**
 * @brief Returns whether the system started (its climate loaded once and its first preset chosen).
 * @param self The system. Must not be NULL.
 */
static inline bool WeatherSystem_IsStarted(const WeatherSystem* self)
{
    return self->_isStarted;
}

/**
 * @brief Returns the seed the system's random streams were made from.
 * @param self The system. Must not be NULL.
 */
static inline uint64_t WeatherSystem_GetSeed(const WeatherSystem* self)
{
    return self->_seed;
}
