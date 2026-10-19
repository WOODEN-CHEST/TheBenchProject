#include <inttypes.h>
#include <stdio.h>
#include "GameCalendar.h"
#include "wr/WRString.h"


// Macros.
/* Howard Hinnant's algorithms count years from 1 March, so that the leap day ends the year. Day number 0
 * (0001-01-01) is this many days after 0000-03-01, the start of that count. */
#define DAYS_FROM_MARCH_START_TO_FIRST_DAY 306
/* An era: the 400-year cycle after which the Gregorian calendar repeats. */
#define YEARS_PER_ERA 400
#define DAYS_PER_ERA 146097
#define DAYS_PER_COMMON_YEAR 365
#define YEARS_PER_LEAP_CYCLE 4
#define YEARS_PER_CENTURY 100
/* The days of an era at which the four-year, the century and the 400-year leap rules step the year-of-era count. */
#define ERA_FOUR_YEAR_STEP 1460
#define ERA_CENTURY_STEP 36524
#define ERA_LAST_DAY 146096
/* The month formula: from March, every five months hold 153 days (31 30 31 30 31), so (153 m + 2) / 5 is the first
 * day of month m counted from March. */
#define MONTH_BLOCK_DAYS 153
#define MONTH_BLOCK_MONTHS 5
#define MONTH_BLOCK_ROUNDING 2
/* Months from March: March is 0, so January and February are 10 and 11 of the year before. */
#define MONTHS_FROM_MARCH_TO_JANUARY 10
#define MARCH 3
#define FEBRUARY 2
/* The positions of the date text "yyyy-mm-dd". */
#define DATE_TEXT_YEAR_DIGITS 4U
#define DATE_TEXT_MONTH_OFFSET 5U
#define DATE_TEXT_DAY_OFFSET 8U
#define DATE_TEXT_TWO_DIGITS 2U
#define DATE_TEXT_FIRST_DASH 4U
#define DATE_TEXT_SECOND_DASH 7U
/* Room for the date text and its terminator (with spare bytes, so no compiler has to prove the fields' ranges). */
#define DATE_TEXT_CAPACITY 32U
#define DECIMAL_BASE 10


// Fields.
static const int32_t CommonYearMonthLengths[GAME_CALENDAR_MONTH_COUNT] =
{
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

// TODO: translate the month and weekday names through translate component keys ("calendar.month.<1-12>",
// "calendar.weekday.<0-6>" and "calendar.weekday.<0-6>.short"), with these English names as their fallbacks, once
// the game has string tables.
static const unsigned char* const MonthNames[GAME_CALENDAR_MONTH_COUNT] =
{
    u8"January", u8"February", u8"March", u8"April", u8"May", u8"June",
    u8"July", u8"August", u8"September", u8"October", u8"November", u8"December"
};

static const unsigned char* const WeekdayNames[GAME_CALENDAR_WEEKDAY_COUNT] =
{
    u8"Monday", u8"Tuesday", u8"Wednesday", u8"Thursday", u8"Friday", u8"Saturday", u8"Sunday"
};

static const unsigned char* const WeekdayShortNames[GAME_CALENDAR_WEEKDAY_COUNT] =
{
    u8"Mon", u8"Tue", u8"Wed", u8"Thu", u8"Fri", u8"Sat", u8"Sun"
};


// Static functions.
/* Days since 0001-01-01 of a valid date: Hinnant's days_from_civil, moved to day 0 = 0001-01-01. Every year of the
 * calendar is positive, so the eras need no rounding toward minus infinity. */
static int64_t ToDayNumber(int32_t year, int32_t month, int32_t day)
{
    int64_t MarchYear = (int64_t)year - ((month < MARCH) ? 1 : 0);
    int64_t Era = MarchYear / YEARS_PER_ERA;
    int64_t YearOfEra = MarchYear - (Era * YEARS_PER_ERA);                                   // [0, 399]
    int64_t MonthFromMarch = (month >= MARCH) ? (month - MARCH) : (month + MONTHS_FROM_MARCH_TO_JANUARY - 1);
    int64_t DayOfYear = (((MONTH_BLOCK_DAYS * MonthFromMarch) + MONTH_BLOCK_ROUNDING) / MONTH_BLOCK_MONTHS)
        + day - 1;                                                                           // [0, 365]
    int64_t DayOfEra = (YearOfEra * DAYS_PER_COMMON_YEAR) + (YearOfEra / YEARS_PER_LEAP_CYCLE)
        - (YearOfEra / YEARS_PER_CENTURY) + DayOfYear;                                       // [0, 146096]
    return (Era * DAYS_PER_ERA) + DayOfEra - DAYS_FROM_MARCH_START_TO_FIRST_DAY;
}

/* The date of a day number inside the calendar: Hinnant's civil_from_days. */
static GameDate ToDate(int64_t dayNumber)
{
    int64_t DaysFromMarchStart = dayNumber + DAYS_FROM_MARCH_START_TO_FIRST_DAY;
    int64_t Era = DaysFromMarchStart / DAYS_PER_ERA;
    int64_t DayOfEra = DaysFromMarchStart - (Era * DAYS_PER_ERA);                            // [0, 146096]
    int64_t YearOfEra = (DayOfEra - (DayOfEra / ERA_FOUR_YEAR_STEP) + (DayOfEra / ERA_CENTURY_STEP)
        - (DayOfEra / ERA_LAST_DAY)) / DAYS_PER_COMMON_YEAR;                                 // [0, 399]
    int64_t DayOfYear = DayOfEra - ((YearOfEra * DAYS_PER_COMMON_YEAR) + (YearOfEra / YEARS_PER_LEAP_CYCLE)
        - (YearOfEra / YEARS_PER_CENTURY));                                                  // [0, 365]
    int64_t MonthFromMarch = ((MONTH_BLOCK_MONTHS * DayOfYear) + MONTH_BLOCK_ROUNDING) / MONTH_BLOCK_DAYS; // [0, 11]
    int64_t Day = DayOfYear - (((MONTH_BLOCK_DAYS * MonthFromMarch) + MONTH_BLOCK_ROUNDING) / MONTH_BLOCK_MONTHS)
        + 1;                                                                                 // [1, 31]
    int64_t Month = (MonthFromMarch < MONTHS_FROM_MARCH_TO_JANUARY) ? (MonthFromMarch + MARCH)
        : (MonthFromMarch - MONTHS_FROM_MARCH_TO_JANUARY + 1);                               // [1, 12]
    int64_t Year = (Era * YEARS_PER_ERA) + YearOfEra + ((Month < MARCH) ? 1 : 0);
    return (GameDate){ .Year = (int32_t)Year, .Month = (int32_t)Month, .Day = (int32_t)Day };
}

static int64_t ClampDayNumber(int64_t dayNumber)
{
    if (dayNumber < GAME_CALENDAR_FIRST_DAY_NUMBER)
    {
        return GAME_CALENDAR_FIRST_DAY_NUMBER;
    }
    return (dayNumber > GAME_CALENDAR_LAST_DAY_NUMBER) ? GAME_CALENDAR_LAST_DAY_NUMBER : dayNumber;
}

/* Reads @p count ASCII decimal digits. */
static bool TryReadDigits(const unsigned char* text, size_t count, int32_t* outValue)
{
    int32_t Value = 0;
    for (size_t Index = 0; Index < count; Index++)
    {
        unsigned char Byte = text[Index];
        if ((Byte < (unsigned char)'0') || (Byte > (unsigned char)'9'))
        {
            return false;
        }
        Value = (Value * DECIMAL_BASE) + (int32_t)(Byte - (unsigned char)'0');
    }
    *outValue = Value;
    return true;
}


// Public functions.
bool GameCalendar_IsLeapYear(int32_t year)
{
    return (((year % YEARS_PER_LEAP_CYCLE) == 0) && ((year % YEARS_PER_CENTURY) != 0)) || ((year % YEARS_PER_ERA) == 0);
}

int32_t GameCalendar_GetDaysInMonth(int32_t year, int32_t month)
{
    if ((month < 1) || (month > GAME_CALENDAR_MONTH_COUNT))
    {
        return 0;
    }
    int32_t Length = CommonYearMonthLengths[month - 1];
    return ((month == FEBRUARY) && GameCalendar_IsLeapYear(year)) ? (Length + 1) : Length;
}

bool GameCalendar_IsDateValid(GameDate date)
{
    if ((date.Year < GAME_CALENDAR_FIRST_YEAR) || (date.Year > GAME_CALENDAR_LAST_YEAR))
    {
        return false;
    }
    return (date.Day >= 1) && (date.Day <= GameCalendar_GetDaysInMonth(date.Year, date.Month));
}

bool GameCalendar_IsDayNumberValid(int64_t dayNumber)
{
    return (dayNumber >= GAME_CALENDAR_FIRST_DAY_NUMBER) && (dayNumber <= GAME_CALENDAR_LAST_DAY_NUMBER);
}

bool GameCalendar_TryGetDayNumber(GameDate date, int64_t* outDayNumber)
{
    if ((outDayNumber == NULL) || !GameCalendar_IsDateValid(date))
    {
        return false;
    }
    *outDayNumber = ToDayNumber(date.Year, date.Month, date.Day);
    return true;
}

GameDate GameCalendar_GetDate(int64_t dayNumber)
{
    return ToDate(ClampDayNumber(dayNumber));
}

GameWeekday GameCalendar_GetWeekday(int64_t dayNumber)
{
    int64_t Remainder = dayNumber % GAME_CALENDAR_WEEKDAY_COUNT;
    if (Remainder < 0)
    {
        Remainder += GAME_CALENDAR_WEEKDAY_COUNT;
    }
    return (GameWeekday)Remainder; // day 0 is a Monday, and GameWeekday counts from Monday = 0
}

int32_t GameCalendar_GetDayOfYear(int64_t dayNumber)
{
    int64_t Clamped = ClampDayNumber(dayNumber);
    GameDate Date = ToDate(Clamped);
    return (int32_t)(Clamped - ToDayNumber(Date.Year, 1, 1)) + 1;
}

const unsigned char* GameCalendar_GetMonthName(int32_t month)
{
    if ((month < 1) || (month > GAME_CALENDAR_MONTH_COUNT))
    {
        return u8"?";
    }
    return MonthNames[month - 1];
}

const unsigned char* GameCalendar_GetWeekdayName(GameWeekday weekday)
{
    if (((int32_t)weekday < 0) || ((int32_t)weekday >= GAME_CALENDAR_WEEKDAY_COUNT))
    {
        return u8"?";
    }
    return WeekdayNames[weekday];
}

const unsigned char* GameCalendar_GetWeekdayShortName(GameWeekday weekday)
{
    if (((int32_t)weekday < 0) || ((int32_t)weekday >= GAME_CALENDAR_WEEKDAY_COUNT))
    {
        return u8"?";
    }
    return WeekdayShortNames[weekday];
}

Error GameCalendar_FormatDate(int64_t dayNumber, GenericBuffer* destination)
{
    if (destination == NULL)
    {
        return Error_Construct2(ErrorCode_IllegalArgument, "GameCalendar_FormatDate: destination must not be NULL.");
    }
    if (!GameCalendar_IsDayNumberValid(dayNumber))
    {
        return Error_Construct4(ErrorCode_ArgumentOutOfRange,
            "GameCalendar_FormatDate: day number %" PRId64 " is outside the calendar (0001-01-01 to 9999-12-31).",
            dayNumber);
    }
    // Formatted on the stack: StringUTF8_Format would allocate a scratch string for every date.
    GameDate Date = ToDate(dayNumber);
    char Text[DATE_TEXT_CAPACITY];
    snprintf(Text, sizeof(Text), "%04" PRId32 "-%02" PRId32 "-%02" PRId32, Date.Year, Date.Month, Date.Day);
    return StringUTF8_CopyTo((const unsigned char*)Text, destination);
}

bool GameCalendar_TryParseDate(const unsigned char* text, size_t byteCount, int64_t* outDayNumber)
{
    if ((text == NULL) || (outDayNumber == NULL) || (byteCount != GAME_CALENDAR_DATE_TEXT_LENGTH))
    {
        return false;
    }
    if ((text[DATE_TEXT_FIRST_DASH] != (unsigned char)'-') || (text[DATE_TEXT_SECOND_DASH] != (unsigned char)'-'))
    {
        return false;
    }
    GameDate Date = { .Year = 0, .Month = 0, .Day = 0 };
    bool AreDigits = TryReadDigits(text, DATE_TEXT_YEAR_DIGITS, &Date.Year)
        && TryReadDigits(text + DATE_TEXT_MONTH_OFFSET, DATE_TEXT_TWO_DIGITS, &Date.Month)
        && TryReadDigits(text + DATE_TEXT_DAY_OFFSET, DATE_TEXT_TWO_DIGITS, &Date.Day);
    return AreDigits && GameCalendar_TryGetDayNumber(Date, outDayNumber);
}
