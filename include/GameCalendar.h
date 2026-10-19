#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"


/**
 * @file GameCalendar.h
 * @brief The game's calendar: pure functions over dates and day numbers in the proleptic Gregorian calendar.
 *
 * The game counts dates as **day numbers**: days since 0001-01-01, which is day 0 (a Monday), as an int64. The
 * calendar covers the years 1 to 9999, day numbers GAME_CALENDAR_FIRST_DAY_NUMBER to GAME_CALENDAR_LAST_DAY_NUMBER.
 * These functions convert between day numbers and dates (year, month, day of the month), give the weekday (Monday =
 * 0), the day of the year, leap years and month lengths, validate dates, name months and weekdays, and read and write
 * the date text "yyyy-mm-dd" that files use.
 *
 * A **date-time** (a moment of the calendar, for stored values: object data, saves, script values) is an int64 count of
 * microseconds since 0001-01-01T00:00:00, from 0 to GAME_CALENDAR_LAST_DATE_TIME; its day number is the count divided
 * by GAME_CALENDAR_MICROSECONDS_PER_DAY. Files write it as "yyyy-mm-ddThh:mm:ss", with up to six digits of fractional
 * seconds when it has any. The conversions are Howard Hinnant's days-from-civil and
 * civil-from-days algorithms (public domain): constant time, no loops over years.
 *
 * Every calendar rule lives in this module. A custom calendar (fantasy months, other month or year lengths, perhaps
 * a `calendar` asset later) would replace these functions, not the code that uses day numbers: the game clock, the
 * property tables' Date type, the moon and the scheduler only count days and ask this module what a day number
 * means.
 *
 * Stateless and thread-safe.
 */


// Macros.
/** @brief The first year of the calendar. */
#define GAME_CALENDAR_FIRST_YEAR 1
/** @brief The last year of the calendar. */
#define GAME_CALENDAR_LAST_YEAR 9999
/** @brief The day number of 0001-01-01, the calendar's first day. */
#define GAME_CALENDAR_FIRST_DAY_NUMBER ((int64_t)0)
/** @brief The day number of 9999-12-31, the calendar's last day. */
#define GAME_CALENDAR_LAST_DAY_NUMBER ((int64_t)3652058)
/** @brief The number of months in a year. */
#define GAME_CALENDAR_MONTH_COUNT 12
/** @brief The number of days in a week. */
#define GAME_CALENDAR_WEEKDAY_COUNT 7
/** @brief The bytes of a date's text, "yyyy-mm-dd", without a terminator. */
#define GAME_CALENDAR_DATE_TEXT_LENGTH ((size_t)10)
/** @brief Microseconds in a second: the unit of a date-time. */
#define GAME_CALENDAR_MICROSECONDS_PER_SECOND ((int64_t)1000000)
/** @brief Microseconds in a day. */
#define GAME_CALENDAR_MICROSECONDS_PER_DAY ((int64_t)86400000000)
/** @brief The last date-time of the calendar: the last microsecond of 9999-12-31. */
#define GAME_CALENDAR_LAST_DATE_TIME \
    (((GAME_CALENDAR_LAST_DAY_NUMBER + 1) * GAME_CALENDAR_MICROSECONDS_PER_DAY) - 1)
/** @brief The most bytes of a date-time's text, "yyyy-mm-ddThh:mm:ss.ffffff", without a terminator. */
#define GAME_CALENDAR_DATE_TIME_TEXT_MAX_LENGTH ((size_t)26)


// Types.
/**
 * @brief A calendar date: year, month and day of the month. A plain value type.
 *
 * Any field values can be stored; GameCalendar_IsDateValid says whether they name a day of the calendar.
 */
typedef struct GameDateStruct
{
    /** @brief The year; 1 to 9999 in a valid date. */
    int32_t Year;
    /** @brief The month, 1 (January) to 12 (December) in a valid date. */
    int32_t Month;
    /** @brief The day of the month, 1 to the month's length in a valid date. */
    int32_t Day;
} GameDate;

/**
 * @brief A day of the week. Monday is 0, as in ISO 8601 (WRDateTime's DayOfWeek, for the wall clock, starts on
 *        Sunday instead).
 */
typedef enum GameWeekdayEnum
{
    /** @brief Monday. */
    GameWeekday_Monday = 0,
    /** @brief Tuesday. */
    GameWeekday_Tuesday = 1,
    /** @brief Wednesday. */
    GameWeekday_Wednesday = 2,
    /** @brief Thursday. */
    GameWeekday_Thursday = 3,
    /** @brief Friday. */
    GameWeekday_Friday = 4,
    /** @brief Saturday. */
    GameWeekday_Saturday = 5,
    /** @brief Sunday. */
    GameWeekday_Sunday = 6
} GameWeekday;


// Functions.
/**
 * @brief Reports whether a year is a leap year: divisible by 4, except centuries not divisible by 400.
 * @param year The year (any value; the proleptic rule continues outside the calendar).
 * @returns true for a leap year (2000, 2024), false otherwise (1900, 2026).
 */
bool GameCalendar_IsLeapYear(int32_t year);

/**
 * @brief Returns the number of days in a month: 28 to 31, February 29 in a leap year.
 * @param year The year (for February).
 * @param month The month, 1 to 12.
 * @returns The month's length, or 0 when @p month is not 1 to 12.
 */
int32_t GameCalendar_GetDaysInMonth(int32_t year, int32_t month);

/**
 * @brief Reports whether a date is a day of the calendar: a year from 1 to 9999, a month from 1 to 12 and a day
 *        that the month has (no 2026-02-29, no day 0).
 * @param date The date.
 * @returns true for a valid date.
 */
bool GameCalendar_IsDateValid(GameDate date);

/**
 * @brief Reports whether a day number lies in the calendar (0001-01-01 to 9999-12-31).
 * @param dayNumber The day number.
 * @returns true when GAME_CALENDAR_FIRST_DAY_NUMBER <= @p dayNumber <= GAME_CALENDAR_LAST_DAY_NUMBER.
 */
bool GameCalendar_IsDayNumberValid(int64_t dayNumber);

/**
 * @brief Converts a date to its day number (days since 0001-01-01).
 * @param date The date.
 * @param outDayNumber [out] Receives the day number; unchanged when the date is invalid. Must not be NULL.
 * @returns true on success; false when @p date is not valid (see GameCalendar_IsDateValid) or @p outDayNumber is
 *          NULL.
 */
bool GameCalendar_TryGetDayNumber(GameDate date, int64_t* outDayNumber);

/**
 * @brief Converts a day number to its date.
 *
 * A day number outside the calendar is clamped to its first or last day first; check it with
 * GameCalendar_IsDayNumberValid where that matters.
 * @param dayNumber The day number.
 * @returns The date (always valid).
 */
GameDate GameCalendar_GetDate(int64_t dayNumber);

/**
 * @brief Returns the weekday of a day number (day 0, 0001-01-01, is a Monday).
 * @param dayNumber The day number (any value: outside the calendar the week simply continues).
 * @returns The weekday.
 */
GameWeekday GameCalendar_GetWeekday(int64_t dayNumber);

/**
 * @brief Returns a day number's day of the year: 1 for 1 January, up to 365 (366 in a leap year) for 31 December.
 * @param dayNumber The day number; clamped into the calendar like GameCalendar_GetDate.
 * @returns The day of the year.
 */
int32_t GameCalendar_GetDayOfYear(int64_t dayNumber);

/**
 * @brief Returns a month's English name, e.g. u8"October".
 * @param month The month, 1 to 12.
 * @returns A static string; u8"?" when @p month is not 1 to 12.
 */
const unsigned char* GameCalendar_GetMonthName(int32_t month);

/**
 * @brief Returns a weekday's English name, e.g. u8"Sunday".
 * @param weekday The weekday.
 * @returns A static string; u8"?" for a value outside GameWeekday.
 */
const unsigned char* GameCalendar_GetWeekdayName(GameWeekday weekday);

/**
 * @brief Returns a weekday's three-letter English abbreviation, e.g. u8"Sun".
 * @param weekday The weekday.
 * @returns A static string; u8"?" for a value outside GameWeekday.
 */
const unsigned char* GameCalendar_GetWeekdayShortName(GameWeekday weekday);

/**
 * @brief Appends a day number's date text, "yyyy-mm-dd" (e.g. "2026-10-18"), to a UTF-8 string buffer.
 *
 * The buffer is one growing string: a trailing NUL terminator is dropped before appending, and the result is
 * NUL-terminated.
 * @param dayNumber The day number; must be valid (see GameCalendar_IsDayNumberValid).
 * @param destination The byte buffer to append to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p destination is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p dayNumber is outside the calendar.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameCalendar_FormatDate(int64_t dayNumber, GenericBuffer* destination);

/**
 * @brief Reads a date text, exactly "yyyy-mm-dd" (four, two and two ASCII digits, e.g. "2026-10-18").
 * @param text The text; it need not be NUL-terminated. May be NULL only when @p byteCount is 0.
 * @param byteCount The number of bytes in @p text.
 * @param outDayNumber [out] Receives the day number; unchanged on failure. Must not be NULL.
 * @returns true on success; false for any other text or a date that does not exist (2026-02-29, 2026-13-01,
 *          0000-01-01).
 */
bool GameCalendar_TryParseDate(const unsigned char* text, size_t byteCount, int64_t* outDayNumber);

/**
 * @brief Reports whether an int64 is a date-time of the calendar (0 to GAME_CALENDAR_LAST_DATE_TIME).
 * @param dateTime The microseconds since 0001-01-01T00:00:00.
 * @returns true if it lies inside the calendar.
 */
bool GameCalendar_IsDateTimeValid(int64_t dateTime);

/**
 * @brief Appends a date-time's text, "yyyy-mm-ddThh:mm:ss" (e.g. "2026-10-18T09:30:00"), to a UTF-8 string buffer,
 *        followed by "." and the fractional seconds (up to six digits, trailing zeros left out) when there are any.
 *
 * The buffer is one growing string: a trailing NUL terminator is dropped before appending, and the result is
 * NUL-terminated.
 * @param dateTime The date-time; must be valid (see GameCalendar_IsDateTimeValid).
 * @param destination The byte buffer to append to; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument if @p destination is NULL; ErrorCode_ArgumentOutOfRange if
 *          @p dateTime is outside the calendar.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the
 *       full set.
 */
Error GameCalendar_FormatDateTime(int64_t dateTime, GenericBuffer* destination);

/**
 * @brief Reads a date-time text: exactly "yyyy-mm-ddThh:mm:ss" (an uppercase T, hours 00 to 23, minutes and seconds
 *        00 to 59), optionally followed by "." and one to six digits of fractional seconds.
 * @param text The text; it need not be NUL-terminated. May be NULL only when @p byteCount is 0.
 * @param byteCount The number of bytes in @p text.
 * @param outDateTime [out] Receives the microseconds since 0001-01-01T00:00:00; unchanged on failure. Must not be NULL.
 * @returns true on success; false for any other text, a date that does not exist or a time out of range.
 */
bool GameCalendar_TryParseDateTime(const unsigned char* text, size_t byteCount, int64_t* outDateTime);
