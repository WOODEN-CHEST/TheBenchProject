#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "GameClock.h"
#include "wr/WRCompile.h"
#include "wr/WRMath.h"
#include "wr/WRString.h"


// Macros.
/** The format version of the clock's property table. */
#define CLOCK_TABLE_VERSION 1U
/** Whole seconds in an hour and a minute, for splitting the time of day. */
#define WHOLE_SECONDS_PER_HOUR 3600
#define WHOLE_SECONDS_PER_MINUTE 60
#define LAST_WHOLE_SECOND_OF_DAY 86399
#define HOURS_PER_DAY 24
/** The largest valid hour, minute and second of a time of day. */
#define LAST_HOUR 23
#define LAST_MINUTE 59
#define LAST_SECOND 59
/** The last double before 86400, nextafter(86400.0, 0.0): the clock stops at this second of 9999-12-31. */
#define LAST_SECOND_OF_DAY 0x1.517FFFFFFFFFFp16
/** Room for "Sun 2026-10-18 09:30:00" and its terminator (with spare bytes, so no compiler has to prove the fields'
 *  ranges). */
#define DEBUG_TEXT_CAPACITY 64U
/** The inspector's category. */
#define CATEGORY_GAME_TIME ((const unsigned char*)u8"Game time")


// Types.
/* The rows of the clock's table, so the setters reach their descriptors without a search. */
typedef enum ClockPropertyEnum
{
    ClockProperty_Day,
    ClockProperty_SecondsOfDay,
    ClockProperty_IsPaused,
    ClockProperty_TimeScale,
    ClockProperty_MaxStepSeconds,
    ClockProperty_Count
} ClockProperty;


// Static functions: the table's hook (the table below refers to it).
/* Any change made through the table may move the time, so the summation carry, which belonged to the old time, goes. */
static void ClockAfterSet(void* instance, const PropertyDescriptor* descriptor)
{
    UNUSED(descriptor);
    GameClock* Clock = instance;
    Clock->_secondsCarry = 0.0;
}


// Fields.
/* The state and the settings, in inspector order. The wire ids are the save schema: never renumber or reuse one; a
 * removed property's id goes into the table's retired list. */
static const PropertyDescriptor ClockProperties[ClockProperty_Count] =
{
    [ClockProperty_Day] =
    {
        .Key = u8"day", .WireId = 1U, .Type = PropertyType_Date, PROPERTY_MEMBER(GameClock, _day),
        .Default.DateValue = GAME_CLOCK_DEFAULT_DAY_NUMBER, .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_GAME_TIME, .DisplayName = u8"Date", .Description = u8"Today's date in the game.",
        .SinceVersion = 1U
    },
    [ClockProperty_SecondsOfDay] =
    {
        .Key = u8"seconds_of_day", .WireId = 2U, .Type = PropertyType_Double,
        PROPERTY_MEMBER(GameClock, _secondsOfDay), .Default.DoubleValue = GAME_CLOCK_DEFAULT_SECONDS_OF_DAY,
        .Rules = PROPERTY_RULES_HALF_OPEN(0.0, GAME_CLOCK_SECONDS_PER_DAY), .Flags = PROPERTY_FLAGS_SETTING,
        .Category = CATEGORY_GAME_TIME, .DisplayName = u8"Time of day",
        .Description = u8"Game seconds since midnight: 0 is midnight, 43200 noon.", .SinceVersion = 1U
    },
    [ClockProperty_IsPaused] =
    {
        .Key = u8"is_paused", .WireId = 3U, .Type = PropertyType_Bool, PROPERTY_MEMBER(GameClock, IsPaused),
        .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_GAME_TIME,
        .DisplayName = u8"Paused",
        .Description = u8"When on, game time stands still; sleeping and skipping still move it.", .SinceVersion = 1U
    },
    [ClockProperty_TimeScale] =
    {
        .Key = u8"time_scale", .WireId = 4U, .Type = PropertyType_Double, PROPERTY_MEMBER(GameClock, _timeScale),
        .Default.DoubleValue = GAME_CLOCK_DEFAULT_TIME_SCALE, .Rules = PROPERTY_RULES_ABOVE(0.0),
        .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_GAME_TIME, .DisplayName = u8"Speed",
        .Description = u8"Game seconds per real second: 72 makes a day last 20 real minutes, 1 is real time.",
        .SinceVersion = 1U
    },
    [ClockProperty_MaxStepSeconds] =
    {
        .Key = u8"max_step_seconds", .WireId = 5U, .Type = PropertyType_Double,
        PROPERTY_MEMBER(GameClock, _maxStepSeconds), .Default.DoubleValue = GAME_CLOCK_DEFAULT_MAX_STEP_SECONDS,
        .Rules = PROPERTY_RULES_ABOVE(0.0), .Flags = PROPERTY_FLAGS_SETTING, .Category = CATEGORY_GAME_TIME,
        .DisplayName = u8"Largest step",
        .Description = u8"The most game time one update may add, so the clock does not leap ahead after a stall.",
        .SinceVersion = 1U
    },
};

static const PropertyTable ClockTable =
{
    .Name = u8"game_clock",
    .Version = CLOCK_TABLE_VERSION,
    .StructSize = sizeof(GameClock),
    .Descriptors = ClockProperties,
    .DescriptorCount = ClockProperty_Count,
    .RetiredWireIds = NULL,
    .RetiredWireIdCount = 0U,
    .AfterSet = ClockAfterSet,
    .Migrate = NULL
};


// Static functions.
static bool IsFiniteDouble(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value);
}

static void ClearCrossings(GameClockCrossings* crossings)
{
    if (crossings != NULL)
    {
        crossings->GameSeconds = 0.0;
        crossings->Midnights = 0;
        crossings->Hours = 0;
    }
}

/* Game seconds from the clock's time to the calendar's last instant. */
static double GetSecondsUntilEnd(const GameClock* self)
{
    double WholeDays = (double)(GAME_CALENDAR_LAST_DAY_NUMBER - self->_day);
    return (WholeDays * GAME_CLOCK_SECONDS_PER_DAY) + (LAST_SECOND_OF_DAY - self->_secondsOfDay);
}

/* What a move from one instant to a later one passed. The hours: 24 for each date change, plus the hour of the day
 * reached minus the hour of the day left (the seconds are in [0, 86400), so the casts round down). */
static void ReportCrossings(int64_t startDay, double startSeconds, int64_t endDay, double endSeconds, double gameSeconds,
    GameClockCrossings* outCrossings)
{
    if (outCrossings == NULL)
    {
        return;
    }
    int64_t StartHour = (int64_t)(startSeconds / GAME_CLOCK_SECONDS_PER_HOUR);
    int64_t EndHour = (int64_t)(endSeconds / GAME_CLOCK_SECONDS_PER_HOUR);
    outCrossings->GameSeconds = gameSeconds;
    outCrossings->Midnights = endDay - startDay;
    outCrossings->Hours = ((endDay - startDay) * HOURS_PER_DAY) + EndHour - StartHour;
}

static void MoveToEnd(GameClock* self)
{
    self->_day = GAME_CALENDAR_LAST_DAY_NUMBER;
    self->_secondsOfDay = LAST_SECOND_OF_DAY;
    self->_secondsCarry = 0.0;
}

/* Moves the clock forward by a finite, positive number of game seconds and reports what it passed. Whole days are
 * split off first, so a long fast-forward keeps the seconds' precision; the rest is added with Kahan's compensated
 * summation, so that millions of small per-tick steps do not drift. Stops at the calendar's end. */
static void MoveForward(GameClock* self, double gameSeconds, GameClockCrossings* outCrossings)
{
    int64_t StartDay = self->_day;
    double StartSeconds = self->_secondsOfDay;
    double SecondsUntilEnd = GetSecondsUntilEnd(self);
    if (gameSeconds >= SecondsUntilEnd)
    {
        MoveToEnd(self);
        ReportCrossings(StartDay, StartSeconds, self->_day, self->_secondsOfDay, SecondsUntilEnd, outCrossings);
        return;
    }

    double WholeDays = 0.0;
    double Remainder = gameSeconds; // a tick: less than a day
    if (gameSeconds >= GAME_CLOCK_SECONDS_PER_DAY)
    {
        WholeDays = floor(gameSeconds / GAME_CLOCK_SECONDS_PER_DAY);
        Remainder = gameSeconds - (WholeDays * GAME_CLOCK_SECONDS_PER_DAY); // exact
        if (Remainder < 0.0)
        {
            WholeDays -= 1.0; // the division rounded up to a whole number of days
            Remainder += GAME_CLOCK_SECONDS_PER_DAY;
        }
    }
    int64_t Day = StartDay + (int64_t)WholeDays;

    double Addend = Remainder - self->_secondsCarry;
    double Seconds = StartSeconds + Addend;
    self->_secondsCarry = (Seconds - StartSeconds) - Addend;
    if (Seconds < 0.0)
    {
        Seconds = 0.0; // a step smaller than the carry, right at midnight
        self->_secondsCarry = 0.0;
    }
    while (Seconds >= GAME_CLOCK_SECONDS_PER_DAY)
    {
        Seconds -= GAME_CLOCK_SECONDS_PER_DAY; // exact
        Day++;
    }

    double MovedSeconds = gameSeconds;
    if (Day > GAME_CALENDAR_LAST_DAY_NUMBER)
    {
        MoveToEnd(self); // rounding carried a step that ends just before the last instant past it
        MovedSeconds = SecondsUntilEnd;
    }
    else
    {
        self->_day = Day;
        self->_secondsOfDay = Seconds;
    }
    ReportCrossings(StartDay, StartSeconds, self->_day, self->_secondsOfDay, MovedSeconds, outCrossings);
}

/* Jumps without reporting anything; the arguments are valid. */
static void SetInstant(GameClock* self, int64_t dayNumber, double secondsOfDay)
{
    self->_day = dayNumber;
    self->_secondsOfDay = secondsOfDay;
    self->_secondsCarry = 0.0;
}

/* Whole seconds since midnight, in [0, 86399] even for a clock whose fields were broken by hand. */
static int32_t GetWholeSecondsOfDay(const GameClock* self)
{
    double Seconds = floor(self->_secondsOfDay);
    if (!(Seconds >= 0.0))
    {
        return 0;
    }
    return (Seconds >= (double)LAST_WHOLE_SECOND_OF_DAY) ? LAST_WHOLE_SECOND_OF_DAY : (int32_t)Seconds;
}

/* Checks @p value against one row of the table, so the setters' rules live in the table only. */
static Error CheckProperty(ClockProperty property, PropertyValue value)
{
    return Properties_CheckValue(&ClockTable, &ClockProperties[property], &value);
}


// Public functions.
const PropertyTable* GameClock_GetPropertyTable(void)
{
    return &ClockTable;
}

void GameClock_Construct(GameClock* self)
{
    if (self == NULL)
    {
        return;
    }
    Error Result = Properties_Construct(&ClockTable, self); // fails only for NULL arguments; zeroes the carry too
    Error_Deconstruct(&Result);
}

void GameClock_Deconstruct(GameClock* self)
{
    Properties_Deconstruct(&ClockTable, self);
}

Error GameClock_Validate(const GameClock* self)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameClock_Validate: self must not be NULL.");
    }
    return Properties_Validate(&ClockTable, self);
}

void GameClock_Advance(GameClock* self, double realSeconds, GameClockCrossings* outCrossings)
{
    ClearCrossings(outCrossings);
    if ((self == NULL) || self->IsPaused || !IsFiniteDouble(realSeconds) || (realSeconds <= 0.0))
    {
        return;
    }
    double Step = realSeconds * self->_timeScale;
    if (Step > self->_maxStepSeconds)
    {
        Step = self->_maxStepSeconds; // also catches an overflow to infinity
    }
    if (!IsFiniteDouble(Step) || (Step <= 0.0))
    {
        return; // only settings broken by hand get here
    }
    MoveForward(self, Step, outCrossings);
}

Error GameClock_AddGameSeconds(GameClock* self, double gameSeconds, GameClockCrossings* outCrossings)
{
    ClearCrossings(outCrossings);
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameClock_AddGameSeconds: self must not be NULL.");
    }
    if (!IsFiniteDouble(gameSeconds) || (gameSeconds < 0.0))
    {
        return Error_Construct2(ErrorCode_ArgumentOutOfRange,
            "GameClock_AddGameSeconds: the game seconds must be finite and >= 0.");
    }
    if (gameSeconds > GetSecondsUntilEnd(self))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "GameClock_AddGameSeconds: %g game seconds would move the clock past 9999-12-31.", gameSeconds);
    }
    if (gameSeconds > 0.0)
    {
        MoveForward(self, gameSeconds, outCrossings);
    }
    return Error_CreateSuccess();
}

Error GameClock_SetDateTime(GameClock* self, int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute,
    int32_t second)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameClock_SetDateTime: self must not be NULL.");
    }
    int64_t DayNumber = 0;
    if (!GameCalendar_TryGetDayNumber((GameDate){ .Year = year, .Month = month, .Day = day }, &DayNumber))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "GameClock_SetDateTime: %04" PRId32 "-%02" PRId32 "-%02" PRId32 " is not a date in the years 1 to 9999.",
            year, month, day);
    }
    if ((hour < 0) || (hour > LAST_HOUR) || (minute < 0) || (minute > LAST_MINUTE) || (second < 0)
        || (second > LAST_SECOND))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "GameClock_SetDateTime: %02" PRId32 ":%02" PRId32 ":%02" PRId32 " is not a time of day (hours 0 to 23, "
            "minutes and seconds 0 to 59).", hour, minute, second);
    }
    double Seconds = ((double)hour * GAME_CLOCK_SECONDS_PER_HOUR) + ((double)minute * GAME_CLOCK_SECONDS_PER_MINUTE)
        + (double)second;
    SetInstant(self, DayNumber, Seconds);
    return Error_CreateSuccess();
}

Error GameClock_SetDayAndTime(GameClock* self, int64_t dayNumber, double secondsOfDay)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameClock_SetDayAndTime: self must not be NULL.");
    }
    Error Result = CheckProperty(ClockProperty_Day, PropertyValue_FromDate(dayNumber));
    if (Result.Code == ErrorCode_Success)
    {
        Result = CheckProperty(ClockProperty_SecondsOfDay, PropertyValue_FromDouble(secondsOfDay));
    }
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    SetInstant(self, dayNumber, secondsOfDay);
    return Error_CreateSuccess();
}

Error GameClock_SetTimeScale(GameClock* self, double timeScale)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameClock_SetTimeScale: self must not be NULL.");
    }
    Error Result = CheckProperty(ClockProperty_TimeScale, PropertyValue_FromDouble(timeScale));
    if (Result.Code == ErrorCode_Success)
    {
        self->_timeScale = timeScale;
    }
    return Result;
}

Error GameClock_SetMaxStepSeconds(GameClock* self, double maxStepSeconds)
{
    if (self == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameClock_SetMaxStepSeconds: self must not be NULL.");
    }
    Error Result = CheckProperty(ClockProperty_MaxStepSeconds, PropertyValue_FromDouble(maxStepSeconds));
    if (Result.Code == ErrorCode_Success)
    {
        self->_maxStepSeconds = maxStepSeconds;
    }
    return Result;
}

GameDate GameClock_GetDate(const GameClock* self)
{
    return GameCalendar_GetDate(self->_day);
}

GameWeekday GameClock_GetWeekday(const GameClock* self)
{
    return GameCalendar_GetWeekday(self->_day);
}

void GameClock_GetHourMinuteSecond(const GameClock* self, int32_t* outHour, int32_t* outMinute, int32_t* outSecond)
{
    int32_t WholeSeconds = GetWholeSecondsOfDay(self);
    if (outHour != NULL)
    {
        *outHour = WholeSeconds / WHOLE_SECONDS_PER_HOUR;
    }
    if (outMinute != NULL)
    {
        *outMinute = (WholeSeconds % WHOLE_SECONDS_PER_HOUR) / WHOLE_SECONDS_PER_MINUTE;
    }
    if (outSecond != NULL)
    {
        *outSecond = WholeSeconds % WHOLE_SECONDS_PER_MINUTE;
    }
}

float GameClock_GetTimeOfDay(const GameClock* self)
{
    return (float)(self->_secondsOfDay / GAME_CLOCK_SECONDS_PER_DAY);
}

double GameClock_GetTotalDays(const GameClock* self)
{
    return (double)self->_day + (self->_secondsOfDay / GAME_CLOCK_SECONDS_PER_DAY);
}

Error GameClock_FormatDebug(const GameClock* self, GenericBuffer* destination)
{
    if ((self == NULL) || (destination == NULL))
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameClock_FormatDebug: self and destination must not be NULL.");
    }
    GameDate Date = GameClock_GetDate(self);
    int32_t Hour = 0;
    int32_t Minute = 0;
    int32_t Second = 0;
    GameClock_GetHourMinuteSecond(self, &Hour, &Minute, &Second);

    // Formatted on the stack: StringUTF8_Format would allocate a scratch string for every line.
    char Text[DEBUG_TEXT_CAPACITY];
    snprintf(Text, sizeof(Text), "%s %04" PRId32 "-%02" PRId32 "-%02" PRId32 " %02" PRId32 ":%02" PRId32 ":%02" PRId32,
        (const char*)GameCalendar_GetWeekdayShortName(GameClock_GetWeekday(self)), Date.Year, Date.Month, Date.Day,
        Hour, Minute, Second);
    return StringUTF8_CopyTo((const unsigned char*)Text, destination);
}
