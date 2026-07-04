#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "WRError.h"
#include "WRComparator.h"
#include "WRMemory.h"


/**
 * DateTime module provides a queryable snapshot of the current wall-clock date and time, similar in
 * spirit to C#'s DateTime struct. A DateTime is a plain value type holding broken-down calendar
 * fields; it owns no resources and is copied by value. Obtain the current time with DateTime_Now
 * (local time) or DateTime_UtcNow (UTC). The platform-specific means of reading the clock is hidden
 * behind this API.
 */


// Types.
/**
 * @brief Day of the week carried by a DateTime.
 *
 * Numbering matches C#'s System.DayOfWeek: Sunday is 0 through Saturday is 6.
 */
typedef enum DayOfWeekEnum
{
    /** @brief Sunday. */
    DayOfWeek_Sunday = 0,
    /** @brief Monday. */
    DayOfWeek_Monday,
    /** @brief Tuesday. */
    DayOfWeek_Tuesday,
    /** @brief Wednesday. */
    DayOfWeek_Wednesday,
    /** @brief Thursday. */
    DayOfWeek_Thursday,
    /** @brief Friday. */
    DayOfWeek_Friday,
    /** @brief Saturday. */
    DayOfWeek_Saturday
} DayOfWeek;

/**
 * @brief Identifies whether a DateTime snapshot is expressed in UTC or local time.
 *
 * DateTime_Now produces DateTimeKind_Local and DateTime_UtcNow produces DateTimeKind_Utc. The
 * Unspecified value exists for DateTime values that were not produced by either query.
 */
typedef enum DateTimeKindEnum
{
    /** @brief The time zone of the value is unspecified. */
    DateTimeKind_Unspecified = 0,
    /** @brief The value is Coordinated Universal Time. */
    DateTimeKind_Utc,
    /** @brief The value is in the host machine's local time zone. */
    DateTimeKind_Local
} DateTimeKind;

/**
 * @brief A broken-down calendar date/time snapshot.
 *
 * A plain value type with no owned resources; copying is a plain value copy. Produced by
 * DateTime_Now and DateTime_UtcNow. The fields are a snapshot and are not kept in sync with the
 * real clock after creation. Although the fields are publicly writable, values obtained from the
 * queries are internally consistent; if you mutate them by hand it is your responsibility to keep
 * them so (WeekDay is not recomputed).
 */
typedef struct DateTimeStruct
{
    /** @brief The full year, e.g. 2026. */
    int32_t Year;
    /** @brief The month of the year, 1 (January) through 12 (December). */
    int32_t Month;
    /** @brief The day of the month, 1 through 31. */
    int32_t Day;
    /** @brief The hour of the day on a 24-hour clock, 0 through 23. */
    int32_t Hour;
    /** @brief The minute of the hour, 0 through 59. */
    int32_t Minute;
    /** @brief The second of the minute, 0 through 60 (60 permits a leap second reported by the OS). */
    int32_t Second;
    /** @brief The millisecond of the second, 0 through 999. */
    int32_t Millisecond;
    /** @brief The day of the week corresponding to the date. */
    DayOfWeek WeekDay;
    /** @brief Whether this snapshot is UTC, local, or unspecified. */
    DateTimeKind Kind;
} DateTime;


// Functions.
/**
 * @brief Reads the host machine's current local date and time.
 *
 * Returns a snapshot of the wall clock in the machine's local time zone. Reading the current time
 * does not fail on the supported platforms, so this function is infallible and returns by value.
 * @returns A DateTime with Kind == DateTimeKind_Local describing the current local time.
 */
DateTime DateTime_Now(void);

/**
 * @brief Reads the host machine's current UTC date and time.
 *
 * Returns a snapshot of the wall clock in Coordinated Universal Time. Reading the current time does
 * not fail on the supported platforms, so this function is infallible and returns by value.
 * @returns A DateTime with Kind == DateTimeKind_Utc describing the current UTC time.
 */
DateTime DateTime_UtcNow(void);

/**
 * @brief Breaks a Unix epoch second down into a DateTime.
 *
 * @p unixSeconds is an absolute instant measured in whole seconds since the Unix epoch
 * (1970-01-01T00:00:00 UTC), the same reference as the C library's time_t. The @p kind argument
 * selects how that instant is rendered: DateTimeKind_Utc breaks it down in UTC, while
 * DateTimeKind_Local (or DateTimeKind_Unspecified, treated as local) applies the host machine's
 * time-zone and daylight-saving rules. The resulting DateTime's Kind is set to DateTimeKind_Utc for
 * a UTC breakdown and DateTimeKind_Local otherwise. The Millisecond field is always 0 (epoch
 * seconds carry no sub-second component).
 * @param unixSeconds Whole seconds since the Unix epoch (may be negative for instants before it).
 * @param kind Whether to render the instant in UTC or local time.
 * @param out [out] Receives the broken-down DateTime. Must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p out is NULL;
 *          ErrorCode_ArgumentOutOfRange if @p unixSeconds cannot be represented in the platform's
 *          time_t (local breakdown only) or the local conversion failed.
 */
Error DateTime_FromUnixSeconds(int64_t unixSeconds, DateTimeKind kind, DateTime* out);

/**
 * @brief Converts a DateTime to a Unix epoch second.
 *
 * Produces the whole number of seconds between the Unix epoch (1970-01-01T00:00:00 UTC) and the
 * instant described by @p self. The value's Kind determines interpretation: DateTimeKind_Local is
 * read as a local time (applying the host time-zone and daylight-saving rules), while
 * DateTimeKind_Utc and DateTimeKind_Unspecified are read as UTC. The Millisecond field is ignored
 * (the result is truncated to whole seconds).
 * @param self The value to convert. Must not be NULL.
 * @param out [out] Receives the epoch second (may be negative). Must not be NULL.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p out is NULL;
 *          ErrorCode_ArgumentOutOfRange if a local-time value is outside the range representable by
 *          the platform's time_t / mktime.
 */
Error DateTime_ToUnixSeconds(const DateTime* self, int64_t* out);

/**
 * @brief Compares two DateTime values chronologically.
 *
 * Compares the calendar fields in order (Year, then Month, Day, Hour, Minute, Second, Millisecond).
 * The Kind field is ignored, so comparing a local value against a UTC value compares their raw
 * fields without any time-zone conversion; this matches C#'s DateTime.Compare.
 * @param a The left-hand value. Must not be NULL.
 * @param b The right-hand value. Must not be NULL.
 * @returns ComparisonResult_ALessThanB if @p a precedes @p b, ComparisonResult_AGreaterThanB if it
 *          follows, otherwise ComparisonResult_AEqualsB.
 */
ComparisonResult DateTime_Compare(const DateTime* a, const DateTime* b);

/**
 * @brief Tests whether two DateTime values represent the same calendar instant.
 *
 * Equivalent to DateTime_Compare(a, b) == ComparisonResult_AEqualsB: all calendar fields must be
 * equal and the Kind field is ignored.
 * @param a The left-hand value. Must not be NULL.
 * @param b The right-hand value. Must not be NULL.
 * @returns true when every calendar field of @p a equals that of @p b, false otherwise.
 */
bool DateTime_Equals(const DateTime* a, const DateTime* b);

/**
 * @brief Appends a canonical textual representation of a DateTime to a UTF-8 string buffer.
 *
 * Writes the timestamp in the fixed format "YYYY-MM-DD HH:MM:SS.mmm" (24-hour clock, zero-padded
 * fields; the year is at least four digits and widens for larger years). Following the framework's
 * growing-string convention, an existing trailing NUL terminator on @p buffer is dropped before the
 * text is appended, and the buffer is left NUL-terminated afterwards, so repeated writes compose
 * into one continuous string. The buffer's contents are not otherwise cleared.
 * @param self The value to format. Must not be NULL.
 * @param buffer The destination byte buffer (element size 1). Must not be NULL and must not be
 *        read-only.
 * @returns ErrorCode_Success on success; ErrorCode_IllegalArgument if @p self or @p buffer is NULL
 *          or @p buffer is not a byte buffer; ErrorCode_InvalidOperation if the buffer is read-only
 *          or the text could not be written. On failure the buffer contents are unspecified.
 */
Error DateTime_ToString(const DateTime* self, GenericBuffer* buffer);

/**
 * @brief Releases a DateTime.
 *
 * A DateTime owns no resources, so this is a no-op provided for API consistency (every type has a
 * deconstructor). Safe to call on any DateTime, including one never initialized.
 * @param self The value to release. May be NULL, in which case the call does nothing.
 */
void DateTime_Deconstruct(DateTime* self);
