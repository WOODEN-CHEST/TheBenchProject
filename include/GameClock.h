#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "GameCalendar.h"
#include "PropertyDescriptor.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"


/**
 * @file GameClock.h
 * @brief The game clock: the in-game date and time that everything reads, advanced at a configurable speed.
 *
 * The clock holds the game's time as a day number (GameCalendar.h: days since 0001-01-01) plus the game seconds since
 * that day's midnight, as a double. Whole days are integers, so precision never degrades over a long game, and the
 * seconds are summed with a compensation term, so millions of small per-tick steps add up without drift.
 *
 * Moving it:
 * - GameClock_Advance, once per update tick: the tick's real seconds times the time scale (game seconds per real
 *   second; 72 by default, a 20-minute day), unless the clock is paused, and never more than MaxStepSeconds at once
 *   (a guard against a huge leap after a stall).
 * - GameClock_AddGameSeconds: a fast-forward (sleeping, skipping), also while paused.
 * - Both report the game seconds they moved and the midnights and whole hours they passed (GameClockCrossings): the
 *   weather moves by those seconds, and the scheduler that comes with the game core will wake on the crossings.
 * - GameClock_SetDateTime and GameClock_SetDayAndTime jump; a jump reports no crossings.
 * - The clock never leaves the calendar: it stops at the last instant of 9999-12-31.
 *
 * Every user of game time reads this one clock: the sky (whoever runs the game writes the clock's time of day and
 * date into the world's environment every tick), the weather (WeatherSystem.h: the game seconds each tick advanced),
 * later the scheduler and NPC schedules. The property table (GameClock_GetPropertyTable) describes the state and the
 * settings, so the clock saves with the game state and scripts and the inspector reach it by key.
 *
 * A plain struct with no owned memory: construct it with GameClock_Construct, release it with GameClock_Deconstruct,
 * copy it through the table (Properties_CopyTo). Not thread-safe.
 */


// Macros.
/** @brief Game seconds in a day. */
#define GAME_CLOCK_SECONDS_PER_DAY 86400.0
/** @brief Game seconds in an hour. */
#define GAME_CLOCK_SECONDS_PER_HOUR 3600.0
/** @brief Game seconds in a minute. */
#define GAME_CLOCK_SECONDS_PER_MINUTE 60.0
/** @brief The default date of a new clock, 2026-10-18 (a Sunday), as a day number. A world environment's date has
 *  the same default, so a new clock and a new sky agree. */
#define GAME_CLOCK_DEFAULT_DAY_NUMBER ((int64_t)739906)
/** @brief The default time of a new clock: noon, in seconds since midnight (a new environment's time of day). */
#define GAME_CLOCK_DEFAULT_SECONDS_OF_DAY 43200.0
/** @brief The default time scale: 72 game seconds per real second, so a game day takes 20 real minutes. */
#define GAME_CLOCK_DEFAULT_TIME_SCALE 72.0
/** @brief The default largest step of one GameClock_Advance: one game hour. */
#define GAME_CLOCK_DEFAULT_MAX_STEP_SECONDS 3600.0


// Types.
/**
 * @brief The game's date and time, its speed and whether it runs.
 *
 * Construct with GameClock_Construct (the table's defaults), release with GameClock_Deconstruct. The `_` fields are
 * read-only outside this module (and the property table, which writes them by offset): read them with the getters,
 * change them with the setters, which validate. IsPaused may be written directly.
 */
typedef struct GameClockStruct
{
    /** @brief The day number (days since 0001-01-01), inside the calendar. Read-only: GameClock_GetDay. */
    int64_t _day;
    /** @brief Game seconds since the day's midnight, finite and in [0, 86400). Read-only: GameClock_GetSecondsOfDay. */
    double _secondsOfDay;
    /** @brief The rounding error of the last step added to _secondsOfDay, taken off the next step (Kahan summation).
     *  Runtime only, a few picoseconds at most; not in the property table, and reset by jumps and by every change made
     *  through the table. */
    double _secondsCarry;
    /** @brief When true, GameClock_Advance does not move the clock; fast-forwards and jumps still do. */
    bool IsPaused;
    /** @brief Game seconds per real second, finite and > 0. Read-only: GameClock_GetTimeScale / SetTimeScale. */
    double _timeScale;
    /** @brief The most game seconds one GameClock_Advance moves, finite and > 0. Read-only:
     *  GameClock_GetMaxStepSeconds / SetMaxStepSeconds. */
    double _maxStepSeconds;
} GameClock;

/**
 * @brief What a move of the clock passed: the game seconds, the midnights and the whole hours.
 *
 * A move from one instant to a later one passes every boundary after the start, up to and including the end: moving
 * from 08:30 onto 09:00 passes 09:00, and moving on from 09:00 does not pass it again. A midnight is also an hour.
 * Systems that run on game time (the weather) take the game seconds from here, so a paused clock, a fast-forward and
 * the largest-step clamp reach them exactly as they reached the clock.
 */
typedef struct GameClockCrossingsStruct
{
    /** @brief The game seconds the clock moved (0 when it did not move). */
    double GameSeconds;
    /** @brief The midnights passed: how many times the date changed. */
    int64_t Midnights;
    /** @brief The whole hours passed (hh:00:00 boundaries, midnights included). */
    int64_t Hours;
} GameClockCrossings;


// Functions: lifecycle and table.
/**
 * @brief Returns the clock's property table: keys "day" (a Date), "seconds_of_day", "is_paused", "time_scale" and
 *        "max_step_seconds", every one saved, shown in the inspector and reachable from scripts.
 *
 * Properties_Set, Properties_ReadJSON / ReadGHDF and Properties_CopyTo on a clock validate like the setters below and
 * reset its summation carry.
 * @returns The table; static, never NULL.
 */
const PropertyTable* GameClock_GetPropertyTable(void);

/**
 * @brief Constructs a clock with the table's defaults: 2026-10-18 12:00:00, running, 72 game seconds per real second,
 *        steps of at most one game hour.
 *
 * Call it on new memory; pair it with GameClock_Deconstruct.
 * @param self The clock to construct. May be NULL, in which case the call does nothing.
 */
void GameClock_Construct(GameClock* self);

/**
 * @brief Releases a clock. It owns nothing yet; call it anyway, so owned fields can be added.
 * @param self The clock, or NULL.
 */
void GameClock_Deconstruct(GameClock* self);

/**
 * @brief Checks a clock against its table's rules (a day inside the calendar, seconds in [0, 86400), a time scale and
 *        a largest step that are finite and > 0).
 *
 * The setters and the table's readers already keep a clock valid; use this on a clock written some other way.
 * @param self The clock; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange naming the first
 *          broken property, e.g. "game_clock.time_scale (_timeScale) must be finite and > 0."
 */
Error GameClock_Validate(const GameClock* self);


// Functions: moving the clock.
/**
 * @brief Moves the clock by one update tick: @p realSeconds times the time scale, at most MaxStepSeconds.
 *
 * Does nothing while paused, for a NULL clock, or for a @p realSeconds that is not finite and > 0. At the end of the
 * calendar the clock stops. Constant time; does not allocate.
 * @param self The clock, or NULL.
 * @param realSeconds The real seconds the tick took (ProgramTime.PassedTime).
 * @param outCrossings [out] Receives the game seconds this call moved and the midnights and hours it passed (zeros
 *        when the clock did not move); may be NULL.
 */
void GameClock_Advance(GameClock* self, double realSeconds, GameClockCrossings* outCrossings);

/**
 * @brief Fast-forwards the clock by @p gameSeconds (sleeping, skipping a scene), whether it is paused or not.
 *
 * Time passes, so the crossings are reported, unlike a jump. Nothing changes on failure.
 * @param self The clock; must not be NULL.
 * @param gameSeconds The game seconds to add; finite and >= 0.
 * @param outCrossings [out] Receives the game seconds moved and the midnights and hours passed (zeros on failure);
 *        may be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange if @p gameSeconds is
 *          not finite or negative, or would move the clock past 9999-12-31.
 */
Error GameClock_AddGameSeconds(GameClock* self, double gameSeconds, GameClockCrossings* outCrossings);

/**
 * @brief Jumps to a date and a time of day given in calendar terms, e.g. (2026, 10, 18, 9, 30, 0).
 *
 * A jump reports no crossings: whoever jumps decides what the skipped time means. Nothing changes on failure.
 * @param self The clock; must not be NULL.
 * @param year The year, 1 to 9999.
 * @param month The month, 1 to 12.
 * @param day The day of the month, 1 to the month's length.
 * @param hour The hour, 0 to 23.
 * @param minute The minute, 0 to 59.
 * @param second The second, 0 to 59.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange naming the date or
 *          the time that does not exist.
 */
Error GameClock_SetDateTime(GameClock* self, int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute,
    int32_t second);

/**
 * @brief Jumps to a day number and a time of day in seconds (the precise form of GameClock_SetDateTime, for tools and
 *        for starting the clock from saved or authored data). Reports no crossings; nothing changes on failure.
 * @param self The clock; must not be NULL.
 * @param dayNumber The day number; inside the calendar.
 * @param secondsOfDay Game seconds since midnight; finite and in [0, 86400).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange (from the table's
 *          rules) for a day outside the calendar or seconds outside [0, 86400).
 */
Error GameClock_SetDayAndTime(GameClock* self, int64_t dayNumber, double secondsOfDay);


// Functions: settings.
/**
 * @brief Sets the speed: game seconds per real second (72 = a 20-minute day, 1 = real time).
 * @param self The clock; must not be NULL.
 * @param timeScale The speed; finite and > 0 (to stop the clock, pause it).
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for an invalid speed
 *          (the clock is unchanged).
 */
Error GameClock_SetTimeScale(GameClock* self, double timeScale);

/**
 * @brief Sets the most game seconds one GameClock_Advance may move.
 * @param self The clock; must not be NULL.
 * @param maxStepSeconds The largest step; finite and > 0.
 * @returns Success; ErrorCode_IllegalArgument if @p self is NULL; ErrorCode_ArgumentOutOfRange for an invalid step
 *          (the clock is unchanged).
 */
Error GameClock_SetMaxStepSeconds(GameClock* self, double maxStepSeconds);


// Functions: reading the time.
/** @brief Returns the day number (days since 0001-01-01). @param self The clock; must not be NULL. @returns The day. */
static inline int64_t GameClock_GetDay(const GameClock* self)
{
    return self->_day;
}

/** @brief Returns the game seconds since midnight, in [0, 86400). @param self The clock; must not be NULL.
 *  @returns The seconds. */
static inline double GameClock_GetSecondsOfDay(const GameClock* self)
{
    return self->_secondsOfDay;
}

/** @brief Returns the speed in game seconds per real second. @param self The clock; must not be NULL.
 *  @returns The time scale. */
static inline double GameClock_GetTimeScale(const GameClock* self)
{
    return self->_timeScale;
}

/** @brief Returns the most game seconds one GameClock_Advance moves. @param self The clock; must not be NULL.
 *  @returns The largest step. */
static inline double GameClock_GetMaxStepSeconds(const GameClock* self)
{
    return self->_maxStepSeconds;
}

/**
 * @brief Returns today's date.
 * @param self The clock; must not be NULL.
 * @returns The date.
 */
GameDate GameClock_GetDate(const GameClock* self);

/**
 * @brief Returns today's weekday.
 * @param self The clock; must not be NULL.
 * @returns The weekday.
 */
GameWeekday GameClock_GetWeekday(const GameClock* self);

/**
 * @brief Returns the time of day as whole hours, minutes and seconds (the seconds' fraction cut off).
 * @param self The clock; must not be NULL.
 * @param outHour [out] Receives 0 to 23; may be NULL.
 * @param outMinute [out] Receives 0 to 59; may be NULL.
 * @param outSecond [out] Receives 0 to 59; may be NULL.
 */
void GameClock_GetHourMinuteSecond(const GameClock* self, int32_t* outHour, int32_t* outMinute, int32_t* outSecond);

/**
 * @brief Returns the time of day as a fraction of the day, for the sky: 0 is midnight, 0.5 noon.
 * @param self The clock; must not be NULL.
 * @returns A value in [0, 1] (1 only by float rounding just before midnight, which the sky treats as midnight).
 */
float GameClock_GetTimeOfDay(const GameClock* self);

/**
 * @brief Returns the time as days since 0001-01-01 00:00, fractions included (Day + SecondsOfDay / 86400), for the
 *        moon's phase and the stars' yearly drift.
 * @param self The clock; must not be NULL.
 * @returns The days (precise to well under a millisecond across the whole calendar).
 */
double GameClock_GetTotalDays(const GameClock* self);

/**
 * @brief Appends the date and time for logs, e.g. "Sun 2026-10-18 09:30:00", to a UTF-8 string buffer.
 *
 * The buffer is one growing string: a trailing NUL terminator is dropped before appending, and the result is
 * NUL-terminated. Formats on the stack; allocates only if the buffer grows.
 * @param self The clock; must not be NULL.
 * @param destination The byte buffer to append to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if an argument is NULL.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameClock_FormatDebug(const GameClock* self, GenericBuffer* destination);
