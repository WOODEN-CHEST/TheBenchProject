#pragma once
#include <stdint.h>


/**
 * Stopwatch module provides a monotonic, high-resolution clock for measuring elapsed time, similar
 * in spirit to C#'s System.Diagnostics.Stopwatch or Java's System.nanoTime(). Unlike the WRDateTime
 * module, which reads the wall clock, this clock is monotonic: its readings never decrease and are
 * unaffected by wall-clock adjustments such as NTP corrections, daylight-saving transitions, or a
 * user manually changing the system clock. Because of that it is the correct clock for measuring
 * durations (frame times, timeouts, fixed-timestep game loops) but useless for telling the calendar
 * date or time of day.
 *
 * The raw reading is exposed by Stopwatch_GetTimestampNanoseconds. Only the difference between two
 * readings is meaningful; the absolute value is measured from an unspecified, platform-defined epoch
 * (typically near system boot) and must not be interpreted as time since any particular date.
 *
 * The Stopwatch value type is a thin convenience built on top: capture a starting timestamp with
 * Stopwatch_StartNew and query the elapsed nanoseconds since it as often as you like. A Stopwatch is
 * a plain value type that owns no resources and is copied by value. The platform-specific means of
 * reading the clock (QueryPerformanceCounter on Windows, clock_gettime(CLOCK_MONOTONIC) on Linux) is
 * hidden behind this API.
 */


// Types.
/**
 * @brief A monotonic elapsed-time measurer, analogous to C#'s System.Diagnostics.Stopwatch.
 *
 * Create one with Stopwatch_StartNew, which records the current monotonic timestamp, then call
 * Stopwatch_ElapsedNanoseconds to read how much time has passed since that point. The stopwatch runs
 * continuously from the moment it is started; there is no stop/resume state. It is a plain value type
 * that owns no resources, so it may be copied freely and stored by value, and Stopwatch_Deconstruct
 * is a no-op provided only for API consistency.
 */
typedef struct StopwatchStruct
{
    /**
     * @brief The monotonic timestamp captured when the stopwatch was started, in nanoseconds.
     *
     * Read-only to other modules; set by Stopwatch_StartNew and Stopwatch_Restart. Its absolute
     * value is measured from an unspecified epoch and is only meaningful when subtracted from a later
     * reading.
     */
    uint64_t _startTimestamp;
} Stopwatch;


// Functions.
/**
 * @brief Reads the current value of the host machine's monotonic high-resolution clock, in nanoseconds.
 *
 * The returned value is monotonic (successive readings never decrease) and independent of wall-clock
 * adjustments (NTP, daylight saving, manual clock changes). Only the difference between two readings
 * is meaningful: the absolute value is measured from an unspecified, platform-defined epoch and must
 * not be treated as time since the Unix epoch or any calendar date. Resolution is sub-millisecond on
 * the supported platforms. Reading the clock does not fail on the supported platforms, so this
 * function is infallible and returns by value.
 * @returns The current monotonic timestamp in nanoseconds since an unspecified epoch.
 */
uint64_t Stopwatch_GetTimestampNanoseconds(void);

/**
 * @brief Creates and starts a new Stopwatch, recording the current monotonic timestamp.
 *
 * Equivalent to C#'s Stopwatch.StartNew(). The returned value begins measuring immediately; pass it
 * (by pointer) to Stopwatch_ElapsedNanoseconds to read the elapsed time.
 * @returns A Stopwatch whose start point is the moment of the call.
 */
Stopwatch Stopwatch_StartNew(void);

/**
 * @brief Returns the number of nanoseconds elapsed since the stopwatch was started (or last restarted).
 *
 * Computes the difference between the current monotonic timestamp and the stopwatch's stored start
 * timestamp. Because the underlying clock is monotonic, the result never decreases across successive
 * calls on the same stopwatch and is never negative.
 * @param self The stopwatch to query. Must not be NULL.
 * @returns The elapsed time in nanoseconds since the stopwatch's start point.
 */
uint64_t Stopwatch_ElapsedNanoseconds(const Stopwatch* self);

/**
 * @brief Restarts the stopwatch, resetting its start point to the current monotonic timestamp.
 *
 * After this call, Stopwatch_ElapsedNanoseconds measures from now. Equivalent to assigning the result
 * of Stopwatch_StartNew.
 * @param self The stopwatch to restart. Must not be NULL.
 */
void Stopwatch_Restart(Stopwatch* self);

/**
 * @brief Releases a Stopwatch.
 *
 * A Stopwatch owns no resources, so this is a no-op provided for API consistency (every type has a
 * deconstructor). Safe to call on any Stopwatch, including one never initialized.
 * @param self The value to release. May be NULL, in which case the call does nothing.
 */
void Stopwatch_Deconstruct(Stopwatch* self);
