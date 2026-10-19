#include "TestSuites.h"
#include "TestText.h"
#include "GameClock.h"
#include "wr/WRString.h"
#include <math.h>
#include <string.h>


// Macros.
/** The update rate the game runs at. */
#define TEST_TICKS_PER_SECOND 480
/** The long run: 1000 game days at 576 game seconds per real second, i.e. 1.2 game seconds per tick, which (unlike
 *  1.5 at the test frame's 720) has no exact binary form, so every addition rounds. 72 000 000 ticks. */
#define LONG_RUN_DAYS 1000
#define LONG_RUN_TIME_SCALE 576.0
#define LONG_RUN_TICKS_PER_DAY 72000
/** How far the long run may end from the exact time, in game seconds. */
#define LONG_RUN_TOLERANCE_SECONDS 0.001
/** Initial capacity of text buffers; they grow as needed. */
#define TEST_TEXT_CAPACITY ((size_t)64)


// Static functions.
static int64_t GetDayNumber(TestRun* run, int32_t year, int32_t month, int32_t day)
{
    int64_t DayNumber = -1;
    TEST_CHECK(run, GameCalendar_TryGetDayNumber((GameDate){ .Year = year, .Month = month, .Day = day }, &DayNumber));
    return DayNumber;
}

/* Whether the clock shows this date and whole time (the seconds' fraction cut off). */
static bool IsAt(const GameClock* clock, int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute,
    int32_t second)
{
    GameDate Date = GameClock_GetDate(clock);
    int32_t Hour = -1;
    int32_t Minute = -1;
    int32_t Second = -1;
    GameClock_GetHourMinuteSecond(clock, &Hour, &Minute, &Second);
    return (Date.Year == year) && (Date.Month == month) && (Date.Day == day) && (Hour == hour) && (Minute == minute)
        && (Second == second);
}

static bool AreCrossings(GameClockCrossings crossings, int64_t midnights, int64_t hours)
{
    return (crossings.Midnights == midnights) && (crossings.Hours == hours);
}

static bool CheckSet(TestRun* run, GameClock* clock, int32_t year, int32_t month, int32_t day, int32_t hour,
    int32_t minute, int32_t second)
{
    return TEST_CHECK_SUCCESS(run, GameClock_SetDateTime(clock, year, month, day, hour, minute, second));
}


// Static functions: tests.
static void DefaultsAreTheTablesDefaults(TestRun* run)
{
    GameClock Clock;
    memset(&Clock, 0xA5, sizeof(Clock)); // garbage, as new memory may hold
    GameClock_Construct(&Clock);
    TEST_CHECK(run, (GameClock_GetDay(&Clock) == GAME_CLOCK_DEFAULT_DAY_NUMBER) && IsAt(&Clock, 2026, 10, 18, 12, 0, 0));
    TEST_CHECK(run, GameClock_GetWeekday(&Clock) == GameWeekday_Sunday);
    TEST_CHECK(run, !Clock.IsPaused && (GameClock_GetTimeScale(&Clock) == GAME_CLOCK_DEFAULT_TIME_SCALE));
    TEST_CHECK(run, GameClock_GetMaxStepSeconds(&Clock) == GAME_CLOCK_DEFAULT_MAX_STEP_SECONDS);
    TEST_CHECK(run, Clock._secondsCarry == 0.0);
    TEST_CHECK(run, GameClock_GetTimeOfDay(&Clock) == 0.5f);
    TEST_CHECK(run, GameClock_GetTotalDays(&Clock) == ((double)GAME_CLOCK_DEFAULT_DAY_NUMBER + 0.5));
    TEST_CHECK_SUCCESS(run, GameClock_Validate(&Clock));
    TEST_CHECK_SUCCESS(run, PropertyTable_Check(GameClock_GetPropertyTable()));
    TEST_CHECK_ERROR(run, GameClock_Validate(NULL), ErrorCode_IllegalArgument);
    GameClock_Construct(NULL);
    GameClock_Deconstruct(&Clock);
    GameClock_Deconstruct(NULL);
}

static void AdvanceFollowsTheTimeScaleAndThePause(TestRun* run)
{
    GameClock Clock;
    GameClock_Construct(&Clock);
    GameClockCrossings Crossings = { .Midnights = -1, .Hours = -1 };

    GameClock_Advance(&Clock, 1.0, &Crossings); // one real second is 72 game seconds
    TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 12, 1, 12) && AreCrossings(Crossings, 0, 0));
    TEST_CHECK(run, Crossings.GameSeconds == 72.0);
    TEST_CHECK_SUCCESS(run, GameClock_SetTimeScale(&Clock, 1.0)); // real time
    GameClock_Advance(&Clock, 0.5, NULL);
    TEST_CHECK(run, GameClock_GetSecondsOfDay(&Clock) == 43272.5);

    Clock.IsPaused = true;
    GameClock_Advance(&Clock, 10.0, &Crossings);
    TEST_CHECK(run, (GameClock_GetSecondsOfDay(&Clock) == 43272.5) && AreCrossings(Crossings, 0, 0));
    TEST_CHECK(run, Crossings.GameSeconds == 0.0);
    TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 7.5, NULL)); // a fast-forward still works while paused
    TEST_CHECK(run, GameClock_GetSecondsOfDay(&Clock) == 43280.0);
    Clock.IsPaused = false;

    // Bad steps do nothing.
    GameClock_Advance(&Clock, NAN, &Crossings);
    GameClock_Advance(&Clock, INFINITY, &Crossings);
    GameClock_Advance(&Clock, -1.0, &Crossings);
    GameClock_Advance(&Clock, 0.0, &Crossings);
    TEST_CHECK(run, (GameClock_GetSecondsOfDay(&Clock) == 43280.0) && AreCrossings(Crossings, 0, 0));
    GameClock_Advance(NULL, 1.0, &Crossings);
    GameClock_Deconstruct(&Clock);
}

static void CrossingsCountMidnightsAndHours(TestRun* run)
{
    GameClock Clock;
    GameClock_Construct(&Clock);
    GameClockCrossings Crossings = { .Midnights = -1, .Hours = -1 };

    // Two midnights in one fast-forward: 23:00 plus 26 hours.
    if (CheckSet(run, &Clock, 2026, 10, 18, 23, 0, 0))
    {
        TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 26.0 * GAME_CLOCK_SECONDS_PER_HOUR, &Crossings));
        TEST_CHECK(run, IsAt(&Clock, 2026, 10, 20, 1, 0, 0) && AreCrossings(Crossings, 2, 26));
        TEST_CHECK(run, Crossings.GameSeconds == (26.0 * GAME_CLOCK_SECONDS_PER_HOUR));
        TEST_CHECK(run, GameClock_GetWeekday(&Clock) == GameWeekday_Tuesday);
    }

    // Arriving on an hour passes it; leaving it does not pass it again.
    if (CheckSet(run, &Clock, 2026, 10, 18, 8, 30, 0))
    {
        TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 30.0 * GAME_CLOCK_SECONDS_PER_MINUTE, &Crossings));
        TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 9, 0, 0) && AreCrossings(Crossings, 0, 1));
        TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 10.0 * GAME_CLOCK_SECONDS_PER_MINUTE, &Crossings));
        TEST_CHECK(run, AreCrossings(Crossings, 0, 0));
    }

    // One second over midnight is a midnight and an hour; ticks report too.
    if (CheckSet(run, &Clock, 2026, 10, 18, 23, 59, 59))
    {
        TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 1.0, &Crossings));
        TEST_CHECK(run, IsAt(&Clock, 2026, 10, 19, 0, 0, 0) && AreCrossings(Crossings, 1, 1));
    }
    if (CheckSet(run, &Clock, 2026, 10, 18, 8, 59, 0))
    {
        GameClock_Advance(&Clock, 1.0, &Crossings);
        TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 9, 0, 12) && AreCrossings(Crossings, 0, 1));
    }

    // A zero fast-forward passes nothing.
    TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 0.0, &Crossings));
    TEST_CHECK(run, AreCrossings(Crossings, 0, 0));
    GameClock_Deconstruct(&Clock);
}

static void MaxStepSecondsClamps(TestRun* run)
{
    GameClock Clock;
    GameClock_Construct(&Clock);
    GameClockCrossings Crossings;

    GameClock_Advance(&Clock, 100.0, &Crossings); // 7200 game seconds asked, one game hour taken
    TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 13, 0, 0) && AreCrossings(Crossings, 0, 1));
    TEST_CHECK(run, Crossings.GameSeconds == GAME_CLOCK_DEFAULT_MAX_STEP_SECONDS);

    TEST_CHECK_SUCCESS(run, GameClock_SetMaxStepSeconds(&Clock, 10.0));
    GameClock_Advance(&Clock, 1.0, NULL);
    TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 13, 0, 10));
    TEST_CHECK_SUCCESS(run, GameClock_SetTimeScale(&Clock, 1e300));
    GameClock_Advance(&Clock, 1e10, NULL); // the product overflows to infinity: still one largest step
    TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 13, 0, 20));
    GameClock_Advance(&Clock, 1.0, NULL);
    TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 13, 0, 30));

    // Invalid settings are refused and change nothing.
    const double Invalid[] = { 0.0, -1.0, NAN, INFINITY };
    for (size_t Index = 0; Index < (sizeof(Invalid) / sizeof(Invalid[0])); Index++)
    {
        TEST_CHECK_ERROR(run, GameClock_SetTimeScale(&Clock, Invalid[Index]), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, GameClock_SetMaxStepSeconds(&Clock, Invalid[Index]), ErrorCode_ArgumentOutOfRange);
    }
    TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 13, 0, 30));
    TEST_CHECK(run, (GameClock_GetTimeScale(&Clock) == 1e300) && (GameClock_GetMaxStepSeconds(&Clock) == 10.0));
    const char* const Fragments[] = { "game_clock.time_scale", "must be finite and > 0", NULL };
    TestText_CheckErrorMessage(run, GameClock_SetTimeScale(&Clock, 0.0), ErrorCode_ArgumentOutOfRange, Fragments);
    TEST_CHECK_ERROR(run, GameClock_SetTimeScale(NULL, 1.0), ErrorCode_IllegalArgument);
    TEST_CHECK_ERROR(run, GameClock_SetMaxStepSeconds(NULL, 1.0), ErrorCode_IllegalArgument);
    GameClock_Deconstruct(&Clock);
}

static void FastForwardCrossesMonthAndYearEnds(TestRun* run)
{
    GameClock Clock;
    GameClock_Construct(&Clock);
    GameClockCrossings Crossings;

    if (CheckSet(run, &Clock, 2026, 10, 31, 23, 30, 0))
    {
        TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, GAME_CLOCK_SECONDS_PER_HOUR, &Crossings));
        TEST_CHECK(run, IsAt(&Clock, 2026, 11, 1, 0, 30, 0) && AreCrossings(Crossings, 1, 1));
    }
    if (CheckSet(run, &Clock, 2026, 12, 31, 22, 0, 0))
    {
        TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 3.0 * GAME_CLOCK_SECONDS_PER_HOUR, &Crossings));
        TEST_CHECK(run, IsAt(&Clock, 2027, 1, 1, 1, 0, 0) && AreCrossings(Crossings, 1, 3));
    }
    if (CheckSet(run, &Clock, 2028, 2, 28, 23, 0, 0)) // into a leap day
    {
        TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 2.0 * GAME_CLOCK_SECONDS_PER_HOUR, &Crossings));
        TEST_CHECK(run, IsAt(&Clock, 2028, 2, 29, 1, 0, 0) && AreCrossings(Crossings, 1, 2));
    }
    if (CheckSet(run, &Clock, 2026, 10, 18, 12, 0, 0)) // a year at once keeps the time of day exactly
    {
        TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 365.0 * GAME_CLOCK_SECONDS_PER_DAY, &Crossings));
        TEST_CHECK(run, IsAt(&Clock, 2027, 10, 18, 12, 0, 0) && AreCrossings(Crossings, 365, 365 * 24));
        TEST_CHECK(run, GameClock_GetSecondsOfDay(&Clock) == 43200.0);
    }

    // Fast-forwards must be finite and >= 0, and nothing changes when they are refused.
    TEST_CHECK_ERROR(run, GameClock_AddGameSeconds(&Clock, -1.0, &Crossings), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_AddGameSeconds(&Clock, NAN, &Crossings), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_AddGameSeconds(&Clock, INFINITY, &Crossings), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, IsAt(&Clock, 2027, 10, 18, 12, 0, 0) && AreCrossings(Crossings, 0, 0));
    TEST_CHECK_ERROR(run, GameClock_AddGameSeconds(NULL, 1.0, NULL), ErrorCode_IllegalArgument);
    GameClock_Deconstruct(&Clock);
}

/* 1000 game days in 480 Hz ticks: the seconds stay in [0, 86400) on every tick, every midnight and hour is reported
 * once, and the clock ends within a millisecond of the exact time. */
static void LongRunDoesNotDrift(TestRun* run)
{
    GameClock Clock;
    GameClock_Construct(&Clock);
    // 12:30, not on an hour: an end a nanosecond early must not lose the last hour's crossing.
    if (!TEST_CHECK_SUCCESS(run, GameClock_SetTimeScale(&Clock, LONG_RUN_TIME_SCALE))
        || !TEST_CHECK_SUCCESS(run, GameClock_SetDateTime(&Clock, 2026, 10, 18, 12, 30, 0)))
    {
        return;
    }
    int64_t StartDay = GameClock_GetDay(&Clock);
    double StartSeconds = GameClock_GetSecondsOfDay(&Clock);
    const double TickSeconds = 1.0 / (double)TEST_TICKS_PER_SECOND;

    const double StepSeconds = TickSeconds * LONG_RUN_TIME_SCALE;
    int64_t Midnights = 0;
    int64_t Hours = 0;
    size_t WrongStepCount = 0U;
    size_t OutOfRangeCount = 0U;
    const int64_t TickCount = (int64_t)LONG_RUN_DAYS * LONG_RUN_TICKS_PER_DAY;
    for (int64_t Tick = 0; Tick < TickCount; Tick++)
    {
        GameClockCrossings Crossings;
        GameClock_Advance(&Clock, TickSeconds, &Crossings);
        Midnights += Crossings.Midnights;
        Hours += Crossings.Hours;
        if (Crossings.GameSeconds != StepSeconds)
        {
            WrongStepCount++;
        }
        double Seconds = GameClock_GetSecondsOfDay(&Clock);
        if (!(Seconds >= 0.0) || !(Seconds < GAME_CLOCK_SECONDS_PER_DAY))
        {
            OutOfRangeCount++;
        }
    }

    TEST_CHECK(run, (OutOfRangeCount == 0U) && (WrongStepCount == 0U));
    TEST_CHECK(run, (Midnights == LONG_RUN_DAYS) && (Hours == ((int64_t)LONG_RUN_DAYS * 24)));
    double Deviation = ((double)(GameClock_GetDay(&Clock) - StartDay - LONG_RUN_DAYS) * GAME_CLOCK_SECONDS_PER_DAY)
        + (GameClock_GetSecondsOfDay(&Clock) - StartSeconds);
    TEST_CHECK(run, fabs(Deviation) < LONG_RUN_TOLERANCE_SECONDS);
    GameClock_Deconstruct(&Clock);
}

static void JumpsAreValidated(TestRun* run)
{
    GameClock Clock;
    GameClock_Construct(&Clock);
    TEST_CHECK(run, CheckSet(run, &Clock, 2024, 2, 29, 23, 59, 59) && IsAt(&Clock, 2024, 2, 29, 23, 59, 59));

    // Each refused jump leaves the clock where it was.
    const char* const DateFragments[] = { "2026-02-29 is not a date", NULL };
    TestText_CheckErrorMessage(run, GameClock_SetDateTime(&Clock, 2026, 2, 29, 12, 0, 0), ErrorCode_ArgumentOutOfRange,
        DateFragments);
    const char* const TimeFragments[] = { "24:00:00 is not a time of day", NULL };
    TestText_CheckErrorMessage(run, GameClock_SetDateTime(&Clock, 2026, 10, 18, 24, 0, 0), ErrorCode_ArgumentOutOfRange,
        TimeFragments);
    TEST_CHECK_ERROR(run, GameClock_SetDateTime(&Clock, 2026, 10, 18, 12, 60, 0), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_SetDateTime(&Clock, 2026, 10, 18, 12, 0, 60), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_SetDateTime(&Clock, 2026, 10, 18, -1, 0, 0), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_SetDateTime(&Clock, 0, 1, 1, 0, 0, 0), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_SetDateTime(NULL, 2026, 10, 18, 0, 0, 0), ErrorCode_IllegalArgument);

    const char* const DayFragments[] = { "game_clock.day", "must be a date in the years 1 to 9999", NULL };
    TestText_CheckErrorMessage(run, GameClock_SetDayAndTime(&Clock, -1, 0.0), ErrorCode_ArgumentOutOfRange,
        DayFragments);
    const char* const SecondsFragments[] = { "game_clock.seconds_of_day", "in [0, 86400)", NULL };
    TestText_CheckErrorMessage(run, GameClock_SetDayAndTime(&Clock, 0, GAME_CLOCK_SECONDS_PER_DAY),
        ErrorCode_ArgumentOutOfRange, SecondsFragments);
    TEST_CHECK_ERROR(run, GameClock_SetDayAndTime(&Clock, 0, -0.5), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_SetDayAndTime(&Clock, 0, NAN), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_SetDayAndTime(&Clock, GAME_CALENDAR_LAST_DAY_NUMBER + 1, 0.0),
        ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameClock_SetDayAndTime(NULL, 0, 0.0), ErrorCode_IllegalArgument);
    TEST_CHECK(run, IsAt(&Clock, 2024, 2, 29, 23, 59, 59));

    // The precise form keeps fractions; a jump drops the summation carry.
    Clock._secondsCarry = 1e-12;
    TEST_CHECK_SUCCESS(run, GameClock_SetDayAndTime(&Clock, GetDayNumber(run, 2026, 10, 18), 34200.25));
    TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 9, 30, 0) && (GameClock_GetSecondsOfDay(&Clock) == 34200.25));
    TEST_CHECK(run, Clock._secondsCarry == 0.0);
    GameClock_Deconstruct(&Clock);
}

static void ClockStopsAtTheCalendarsEnd(TestRun* run)
{
    GameClock Clock;
    GameClock_Construct(&Clock);
    GameClockCrossings Crossings;
    if (!TEST_CHECK_SUCCESS(run, GameClock_SetDayAndTime(&Clock, GAME_CALENDAR_LAST_DAY_NUMBER, 86000.0)))
    {
        return;
    }

    // A fast-forward past 9999-12-31 is refused; a tick stops at its last instant.
    TEST_CHECK_ERROR(run, GameClock_AddGameSeconds(&Clock, 1000.0, &Crossings), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK(run, GameClock_GetSecondsOfDay(&Clock) == 86000.0);
    GameClock_Advance(&Clock, 100.0, &Crossings); // 3600 game seconds asked, 400 left
    TEST_CHECK(run, IsAt(&Clock, 9999, 12, 31, 23, 59, 59) && (GameClock_GetDay(&Clock) == GAME_CALENDAR_LAST_DAY_NUMBER));
    TEST_CHECK(run, (GameClock_GetSecondsOfDay(&Clock) == nextafter(GAME_CLOCK_SECONDS_PER_DAY, 0.0))
        && AreCrossings(Crossings, 0, 0));
    TEST_CHECK(run, Crossings.GameSeconds == (nextafter(GAME_CLOCK_SECONDS_PER_DAY, 0.0) - 86000.0)); // what was left
    TEST_CHECK_SUCCESS(run, GameClock_Validate(&Clock));

    double End = GameClock_GetSecondsOfDay(&Clock);
    GameClock_Advance(&Clock, 1.0, &Crossings);
    TEST_CHECK(run, (GameClock_GetSecondsOfDay(&Clock) == End) && AreCrossings(Crossings, 0, 0));
    TEST_CHECK(run, Crossings.GameSeconds == 0.0);
    TEST_CHECK_SUCCESS(run, GameClock_AddGameSeconds(&Clock, 0.0, NULL));
    GameClock_Deconstruct(&Clock);
}

static void TimeReadsInEveryForm(TestRun* run)
{
    GameClock Clock;
    GameClock_Construct(&Clock);
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, TEST_TEXT_CAPACITY, sizeof(unsigned char));
    if (CheckSet(run, &Clock, 2026, 10, 18, 9, 30, 5))
    {
        int32_t Hour = -1;
        int32_t Minute = -1;
        int32_t Second = -1;
        GameClock_GetHourMinuteSecond(&Clock, &Hour, &Minute, &Second);
        TEST_CHECK(run, (Hour == 9) && (Minute == 30) && (Second == 5));
        GameClock_GetHourMinuteSecond(&Clock, NULL, NULL, NULL);
        TEST_CHECK(run, fabsf(GameClock_GetTimeOfDay(&Clock) - (float)(34205.0 / 86400.0)) < 1e-7f);
        TEST_CHECK(run, GameClock_GetTotalDays(&Clock) == ((double)GAME_CLOCK_DEFAULT_DAY_NUMBER + (34205.0 / 86400.0)));

        TEST_CHECK_SUCCESS(run, StringUTF8_CopyTo((const unsigned char*)"now: ", &Text));
        if (TEST_CHECK_SUCCESS(run, GameClock_FormatDebug(&Clock, &Text)))
        {
            TEST_CHECK(run, strcmp((const char*)Text._data, "now: Sun 2026-10-18 09:30:05") == 0);
        }
    }
    // The fraction of a second is cut off, never rounded up into the next second or day.
    if (TEST_CHECK_SUCCESS(run, GameClock_SetDayAndTime(&Clock, GAME_CLOCK_DEFAULT_DAY_NUMBER, 86399.999)))
    {
        TEST_CHECK(run, IsAt(&Clock, 2026, 10, 18, 23, 59, 59));
    }
    TEST_CHECK_ERROR(run, GameClock_FormatDebug(&Clock, NULL), ErrorCode_IllegalArgument);
    Memory_Free(Text._data);
    GameClock_Deconstruct(&Clock);
}

/* The clock saves through its table: JSON (the day as "yyyy-mm-dd", defaults left out) and GHDF both round-trip, and
 * a read drops the summation carry. */
static void TableRoundTripsJSONAndGHDF(TestRun* run)
{
    JSONObjectPool* Pool = NULL;
    GHDFObjectPool* BinaryPool = NULL;
    if (!TEST_CHECK_SUCCESS(run, JSONObjectPool_Create(&Pool)) || !TEST_CHECK_SUCCESS(run, GHDFObjectPool_Create(&BinaryPool)))
    {
        if (Pool != NULL)
        {
            TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
        }
        return;
    }
    const PropertyTable* Table = GameClock_GetPropertyTable();
    GameClock Source;
    GameClock Restored;
    GameClock_Construct(&Source);
    GameClock_Construct(&Restored);
    TEST_CHECK_SUCCESS(run, GameClock_SetDayAndTime(&Source, GetDayNumber(run, 2030, 6, 1), 27930.25));
    Source.IsPaused = true;
    TEST_CHECK_SUCCESS(run, GameClock_SetTimeScale(&Source, 1.0 / 3.0));
    TEST_CHECK_SUCCESS(run, GameClock_SetMaxStepSeconds(&Source, 60.0));

    JSONCompound* Compound = NULL;
    if (TEST_CHECK_SUCCESS(run, JSONObjectPool_BorrowCompound(Pool, &Compound)))
    {
        TEST_CHECK_SUCCESS(run, Properties_WriteJSON(Table, &Source, Pool, Compound, NULL));
        TEST_CHECK(run, JSONCompound_GetEntryCount(Compound) == 5U);
        JSONObjectValue Day;
        if (TEST_CHECK_SUCCESS(run, JSONCompound_GetVerified(Compound, u8"day", JSONValueType_String, &Day)))
        {
            TEST_CHECK(run, (Day.Value.String->_count == 10U) && (memcmp(Day.Value.String->_data, "2030-06-01", 10U) == 0));
        }
        Restored._secondsCarry = 1e-12;
        TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Compound, &Restored, NULL));
        TEST_CHECK(run, Properties_Equals(Table, &Source, &Restored) && (Restored._secondsCarry == 0.0));
        TEST_CHECK(run, GameClock_GetTimeScale(&Restored) == (1.0 / 3.0)); // a double keeps every bit
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnCompound(Pool, Compound, true));
    }

    // A file with only a date and a time.
    JSONObjectValue Value;
    if (TestText_ParseJSON(run, Pool, "{\"day\": \"2026-12-24\", \"seconds_of_day\": 64800}", &Value))
    {
        TEST_CHECK_SUCCESS(run, Properties_ReadJSON(Table, Value.Value.Compound, &Restored, NULL));
        TEST_CHECK(run, IsAt(&Restored, 2026, 12, 24, 18, 0, 0) && !Restored.IsPaused);
        TEST_CHECK(run, GameClock_GetTimeScale(&Restored) == GAME_CLOCK_DEFAULT_TIME_SCALE);
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }
    if (TestText_ParseJSON(run, Pool, "{\"day\": \"2026-02-30\"}", &Value))
    {
        const char* const Fragments[] = { "game_clock.day", "yyyy-mm-dd", NULL };
        TestText_CheckErrorMessage(run, Properties_ReadJSON(Table, Value.Value.Compound, &Restored, NULL),
            ErrorCode_InvalidJSON, Fragments);
        TEST_CHECK(run, IsAt(&Restored, 2026, 12, 24, 18, 0, 0));
        TEST_CHECK_SUCCESS(run, JSONObjectPool_ReturnValue(Pool, &Value));
    }

    GHDFCompound* Binary = NULL;
    if (TEST_CHECK_SUCCESS(run, GHDFObjectPool_BorrowCompound(BinaryPool, &Binary)))
    {
        TEST_CHECK_SUCCESS(run, Properties_WriteGHDF(Table, &Source, BinaryPool, Binary, NULL));
        GHDFObjectValue Entry;
        if (TEST_CHECK_SUCCESS(run, GHDFCompound_GetVerified(Binary, 1U,
            GHDF_CreateRegularType(GHDFValueType_EncodedInteger), &Entry)))
        {
            TEST_CHECK(run, Entry.Value.EncodedInteger == GameClock_GetDay(&Source)); // the day number
        }
        TEST_CHECK_SUCCESS(run, GHDFCompound_GetVerified(Binary, 2U, GHDF_CreateRegularType(GHDFValueType_Double), &Entry));
        TEST_CHECK_SUCCESS(run, Properties_ReadGHDF(Table, Binary, &Restored, NULL));
        TEST_CHECK(run, Properties_Equals(Table, &Source, &Restored));
        TEST_CHECK_SUCCESS(run, GHDFObjectPool_ReturnCompound(BinaryPool, Binary, true));
    }
    GameClock_Deconstruct(&Source);
    GameClock_Deconstruct(&Restored);
    TEST_CHECK_SUCCESS(run, GHDFObjectPool_Deconstruct(BinaryPool));
    TEST_CHECK_SUCCESS(run, JSONObjectPool_Deconstruct(Pool));
}


// Public functions.
void GameClockTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "defaults_are_the_tables_defaults", DefaultsAreTheTablesDefaults },
        { "advance_follows_the_time_scale_and_the_pause", AdvanceFollowsTheTimeScaleAndThePause },
        { "crossings_count_midnights_and_hours", CrossingsCountMidnightsAndHours },
        { "max_step_seconds_clamps", MaxStepSecondsClamps },
        { "fast_forward_crosses_month_and_year_ends", FastForwardCrossesMonthAndYearEnds },
        { "long_run_does_not_drift", LongRunDoesNotDrift },
        { "jumps_are_validated", JumpsAreValidated },
        { "clock_stops_at_the_calendars_end", ClockStopsAtTheCalendarsEnd },
        { "time_reads_in_every_form", TimeReadsInEveryForm },
        { "table_round_trips_json_and_ghdf", TableRoundTripsJSONAndGHDF },
    };
    TestRun_RunSuite(run, "GameClock", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
