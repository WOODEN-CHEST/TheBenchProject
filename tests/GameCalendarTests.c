#include "TestSuites.h"
#include "GameCalendar.h"
#include <string.h>


// Macros.
/** The days from 1600-01-01 to 2400-12-31, both included: 801 years, 195 of them leap years. */
#define DAYS_1600_TO_2400 ((int64_t)292560)
/** Known day numbers (days since 0001-01-01): the Unix epoch, the year 2000 and the tests' sample date. */
#define DAY_1970_01_01 ((int64_t)719162)
#define DAY_2000_01_01 ((int64_t)730119)
#define DAY_2026_10_18 ((int64_t)739906)


// Static functions.
static bool AreDatesEqual(GameDate a, int32_t year, int32_t month, int32_t day)
{
    return (a.Year == year) && (a.Month == month) && (a.Day == day);
}

/* Whether @p next is the calendar day after @p previous: the next day of the month, or the first of the next month
 * after the month's last day. */
static bool IsFollowingDate(GameDate previous, GameDate next)
{
    if ((next.Year == previous.Year) && (next.Month == previous.Month))
    {
        return next.Day == (previous.Day + 1);
    }
    bool WasLastDay = previous.Day == GameCalendar_GetDaysInMonth(previous.Year, previous.Month);
    bool IsNextMonth = (next.Year == previous.Year) && (next.Month == (previous.Month + 1));
    bool IsNextYear = (next.Year == (previous.Year + 1)) && (previous.Month == 12) && (next.Month == 1);
    return WasLastDay && (next.Day == 1) && (IsNextMonth || IsNextYear);
}

static int64_t GetDayNumber(TestRun* run, int32_t year, int32_t month, int32_t day)
{
    int64_t DayNumber = -1;
    TEST_CHECK(run, GameCalendar_TryGetDayNumber((GameDate){ .Year = year, .Month = month, .Day = day }, &DayNumber));
    return DayNumber;
}

static bool TryParse(const char* text, int64_t* outDayNumber)
{
    return GameCalendar_TryParseDate((const unsigned char*)text, strlen(text), outDayNumber);
}


// Static functions: tests.
/* Every day from 1600-01-01 to 2400-12-31 converts to a date and back, and each day's date follows the one before. */
static void EveryDayRoundTrips(TestRun* run)
{
    int64_t First = GetDayNumber(run, 1600, 1, 1);
    int64_t Last = GetDayNumber(run, 2400, 12, 31);
    TEST_CHECK(run, (Last - First + 1) == DAYS_1600_TO_2400);

    GameDate Previous = GameCalendar_GetDate(First - 1);
    TEST_CHECK(run, AreDatesEqual(Previous, 1599, 12, 31));
    size_t FailureCount = 0U;
    for (int64_t Day = First; Day <= Last; Day++)
    {
        GameDate Date = GameCalendar_GetDate(Day);
        int64_t Back = -1;
        if (!GameCalendar_IsDateValid(Date) || !GameCalendar_TryGetDayNumber(Date, &Back) || (Back != Day)
            || !IsFollowingDate(Previous, Date))
        {
            FailureCount++;
        }
        Previous = Date;
    }
    TEST_CHECK(run, FailureCount == 0U);
}

static void KnownDaysHaveTheirNumbersAndWeekdays(TestRun* run)
{
    TEST_CHECK(run, GetDayNumber(run, 1, 1, 1) == GAME_CALENDAR_FIRST_DAY_NUMBER);
    TEST_CHECK(run, GetDayNumber(run, 9999, 12, 31) == GAME_CALENDAR_LAST_DAY_NUMBER);
    TEST_CHECK(run, GetDayNumber(run, 1970, 1, 1) == DAY_1970_01_01);
    TEST_CHECK(run, GetDayNumber(run, 2000, 1, 1) == DAY_2000_01_01);
    TEST_CHECK(run, GetDayNumber(run, 2026, 10, 18) == DAY_2026_10_18);

    TEST_CHECK(run, GameCalendar_GetWeekday(GAME_CALENDAR_FIRST_DAY_NUMBER) == GameWeekday_Monday);
    TEST_CHECK(run, GameCalendar_GetWeekday(DAY_1970_01_01) == GameWeekday_Thursday);
    TEST_CHECK(run, GameCalendar_GetWeekday(DAY_2000_01_01) == GameWeekday_Saturday);
    TEST_CHECK(run, GameCalendar_GetWeekday(DAY_2026_10_18) == GameWeekday_Sunday);
    TEST_CHECK(run, GameCalendar_GetWeekday(GAME_CALENDAR_LAST_DAY_NUMBER) == GameWeekday_Friday);
    TEST_CHECK(run, GameCalendar_GetWeekday(-1) == GameWeekday_Sunday); // the week continues before the calendar

    TEST_CHECK(run, AreDatesEqual(GameCalendar_GetDate(DAY_2026_10_18), 2026, 10, 18));
    TEST_CHECK(run, AreDatesEqual(GameCalendar_GetDate(-5), 1, 1, 1)); // clamped to the calendar
    TEST_CHECK(run, AreDatesEqual(GameCalendar_GetDate(GAME_CALENDAR_LAST_DAY_NUMBER + 10), 9999, 12, 31));
    TEST_CHECK(run, !GameCalendar_IsDayNumberValid(-1) && GameCalendar_IsDayNumberValid(0));
    TEST_CHECK(run, GameCalendar_IsDayNumberValid(GAME_CALENDAR_LAST_DAY_NUMBER)
        && !GameCalendar_IsDayNumberValid(GAME_CALENDAR_LAST_DAY_NUMBER + 1));
}

static void LeapYearsAndMonthLengths(TestRun* run)
{
    TEST_CHECK(run, !GameCalendar_IsLeapYear(1900) && GameCalendar_IsLeapYear(2000) && GameCalendar_IsLeapYear(2024));
    TEST_CHECK(run, !GameCalendar_IsLeapYear(2026) && !GameCalendar_IsLeapYear(2100) && GameCalendar_IsLeapYear(2400));

    TEST_CHECK(run, GameCalendar_GetDaysInMonth(2024, 2) == 29);
    TEST_CHECK(run, GameCalendar_GetDaysInMonth(2026, 2) == 28);
    TEST_CHECK(run, GameCalendar_GetDaysInMonth(1900, 2) == 28);
    TEST_CHECK(run, GameCalendar_GetDaysInMonth(2000, 2) == 29);
    TEST_CHECK(run, (GameCalendar_GetDaysInMonth(2026, 1) == 31) && (GameCalendar_GetDaysInMonth(2026, 4) == 30));
    TEST_CHECK(run, GameCalendar_GetDaysInMonth(2026, 12) == 31);
    TEST_CHECK(run, (GameCalendar_GetDaysInMonth(2026, 0) == 0) && (GameCalendar_GetDaysInMonth(2026, 13) == 0));

    TEST_CHECK(run, GameCalendar_GetDayOfYear(GetDayNumber(run, 2026, 1, 1)) == 1);
    TEST_CHECK(run, GameCalendar_GetDayOfYear(DAY_2026_10_18) == 291);
    TEST_CHECK(run, GameCalendar_GetDayOfYear(GetDayNumber(run, 2026, 12, 31)) == 365);
    TEST_CHECK(run, GameCalendar_GetDayOfYear(GetDayNumber(run, 2024, 3, 1)) == 61);
    TEST_CHECK(run, GameCalendar_GetDayOfYear(GetDayNumber(run, 2024, 12, 31)) == 366);
}

static void InvalidDatesAreRejected(TestRun* run)
{
    const GameDate Invalid[] =
    {
        { .Year = 2026, .Month = 2, .Day = 29 },  // not a leap year
        { .Year = 2026, .Month = 13, .Day = 1 },
        { .Year = 2026, .Month = 0, .Day = 1 },
        { .Year = 2026, .Month = 10, .Day = 0 },
        { .Year = 2026, .Month = 10, .Day = 32 },
        { .Year = 2026, .Month = 4, .Day = 31 },
        { .Year = 0, .Month = 1, .Day = 1 },      // before the calendar
        { .Year = 10000, .Month = 1, .Day = 1 },  // after it
        { .Year = -400, .Month = 1, .Day = 1 },
    };
    for (size_t Index = 0; Index < (sizeof(Invalid) / sizeof(Invalid[0])); Index++)
    {
        int64_t DayNumber = 12345;
        TEST_CHECK(run, !GameCalendar_IsDateValid(Invalid[Index]));
        TEST_CHECK(run, !GameCalendar_TryGetDayNumber(Invalid[Index], &DayNumber) && (DayNumber == 12345));
    }
    TEST_CHECK(run, GameCalendar_IsDateValid((GameDate){ .Year = 2024, .Month = 2, .Day = 29 }));
    TEST_CHECK(run, !GameCalendar_TryGetDayNumber((GameDate){ .Year = 2026, .Month = 1, .Day = 1 }, NULL));
}

static void NamesAreEnglish(TestRun* run)
{
    TEST_CHECK(run, strcmp((const char*)GameCalendar_GetMonthName(1), "January") == 0);
    TEST_CHECK(run, strcmp((const char*)GameCalendar_GetMonthName(10), "October") == 0);
    TEST_CHECK(run, strcmp((const char*)GameCalendar_GetMonthName(12), "December") == 0);
    TEST_CHECK(run, strcmp((const char*)GameCalendar_GetMonthName(13), "?") == 0);
    TEST_CHECK(run, strcmp((const char*)GameCalendar_GetWeekdayName(GameWeekday_Monday), "Monday") == 0);
    TEST_CHECK(run, strcmp((const char*)GameCalendar_GetWeekdayName(GameWeekday_Sunday), "Sunday") == 0);
    TEST_CHECK(run, strcmp((const char*)GameCalendar_GetWeekdayShortName(GameWeekday_Sunday), "Sun") == 0);
    TEST_CHECK(run, strcmp((const char*)GameCalendar_GetWeekdayShortName((GameWeekday)7), "?") == 0);
}

static void DateTextReadsAndWrites(TestRun* run)
{
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, GameCalendar_FormatDate(DAY_2026_10_18, &Text)))
    {
        TEST_CHECK(run, strcmp((const char*)Text._data, "2026-10-18") == 0);
    }
    // One growing string: the next date continues it.
    TEST_CHECK_SUCCESS(run, GameCalendar_FormatDate(GAME_CALENDAR_FIRST_DAY_NUMBER, &Text));
    TEST_CHECK(run, strcmp((const char*)Text._data, "2026-10-180001-01-01") == 0);
    GenericBuffer_Clear(&Text);
    TEST_CHECK_SUCCESS(run, GameCalendar_FormatDate(GAME_CALENDAR_LAST_DAY_NUMBER, &Text));
    TEST_CHECK(run, strcmp((const char*)Text._data, "9999-12-31") == 0);
    TEST_CHECK_ERROR(run, GameCalendar_FormatDate(-1, &Text), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameCalendar_FormatDate(DAY_2026_10_18, NULL), ErrorCode_IllegalArgument);
    Memory_Free(Text._data);

    int64_t DayNumber = -1;
    TEST_CHECK(run, TryParse("2026-10-18", &DayNumber) && (DayNumber == DAY_2026_10_18));
    TEST_CHECK(run, TryParse("0001-01-01", &DayNumber) && (DayNumber == GAME_CALENDAR_FIRST_DAY_NUMBER));
    TEST_CHECK(run, TryParse("9999-12-31", &DayNumber) && (DayNumber == GAME_CALENDAR_LAST_DAY_NUMBER));
    TEST_CHECK(run, TryParse("2024-02-29", &DayNumber));

    const char* const Rejected[] =
    {
        "2026-02-29", "2026-13-01", "2026-10-00", "0000-01-01", "2026-10-1", "2026-10-180", "2026/10/18",
        "2026-1a-18", "+026-10-18", " 2026-10-18", "2026-10-18 ", "26-10-2018", "", "2026-10-18T12:00",
    };
    for (size_t Index = 0; Index < (sizeof(Rejected) / sizeof(Rejected[0])); Index++)
    {
        DayNumber = 77;
        TEST_CHECK(run, !TryParse(Rejected[Index], &DayNumber) && (DayNumber == 77));
    }
    TEST_CHECK(run, !GameCalendar_TryParseDate(NULL, 0U, &DayNumber));
    TEST_CHECK(run, !GameCalendar_TryParseDate((const unsigned char*)"2026-10-18", GAME_CALENDAR_DATE_TEXT_LENGTH, NULL));
}


/* Formats @p dateTime into a fresh buffer and compares the text. */
static void CheckDateTimeText(TestRun* run, int64_t dateTime, const char* expected)
{
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    if (TEST_CHECK_SUCCESS(run, GameCalendar_FormatDateTime(dateTime, &Text)))
    {
        TEST_CHECK(run, strcmp((const char*)Text._data, expected) == 0);
        int64_t ReadBack = -1;
        TEST_CHECK(run, GameCalendar_TryParseDateTime(Text._data, strlen(expected), &ReadBack) && (ReadBack == dateTime));
    }
    Memory_Free(Text._data);
}

static void DateTimeTextReadsAndWrites(TestRun* run)
{
    const int64_t Second = GAME_CALENDAR_MICROSECONDS_PER_SECOND;
    const int64_t Morning = (DAY_2026_10_18 * GAME_CALENDAR_MICROSECONDS_PER_DAY) + (((9 * 60) + 30) * 60 * Second);
    CheckDateTimeText(run, Morning, "2026-10-18T09:30:00");
    CheckDateTimeText(run, Morning + (Second / 2), "2026-10-18T09:30:00.5");
    CheckDateTimeText(run, Morning + 1, "2026-10-18T09:30:00.000001");
    CheckDateTimeText(run, 0, "0001-01-01T00:00:00");
    CheckDateTimeText(run, GAME_CALENDAR_LAST_DATE_TIME, "9999-12-31T23:59:59.999999");

    int64_t DateTime = -1;
    TEST_CHECK(run, GameCalendar_TryParseDateTime((const unsigned char*)"2026-10-18T09:30:00.25", 22U, &DateTime)
        && (DateTime == (Morning + (Second / 4))));
    const char* const Rejected[] =
    {
        "2026-10-18", "2026-10-18 09:30:00", "2026-10-18t09:30:00", "2026-10-18T24:00:00", "2026-10-18T09:60:00",
        "2026-10-18T09:30:60", "2026-10-18T09:30:00.", "2026-10-18T09:30:00.1234567", "2026-02-29T09:30:00",
        "2026-10-18T9:30:00", "2026-10-18T09:30:00Z", "2026-10-18T09:30",
    };
    for (size_t Index = 0; Index < (sizeof(Rejected) / sizeof(Rejected[0])); Index++)
    {
        DateTime = 77;
        TEST_CHECK(run, !GameCalendar_TryParseDateTime((const unsigned char*)Rejected[Index], strlen(Rejected[Index]),
            &DateTime) && (DateTime == 77));
    }

    TEST_CHECK(run, GameCalendar_IsDateTimeValid(0) && GameCalendar_IsDateTimeValid(GAME_CALENDAR_LAST_DATE_TIME));
    TEST_CHECK(run, !GameCalendar_IsDateTimeValid(-1) && !GameCalendar_IsDateTimeValid(GAME_CALENDAR_LAST_DATE_TIME + 1));
    GenericBuffer Text;
    GenericBuffer_AllocateVariable(&Text, 0U, sizeof(unsigned char));
    TEST_CHECK_ERROR(run, GameCalendar_FormatDateTime(-1, &Text), ErrorCode_ArgumentOutOfRange);
    TEST_CHECK_ERROR(run, GameCalendar_FormatDateTime(0, NULL), ErrorCode_IllegalArgument);
    Memory_Free(Text._data);
}


// Public functions.
void GameCalendarTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "every_day_round_trips", EveryDayRoundTrips },
        { "known_days_have_their_numbers_and_weekdays", KnownDaysHaveTheirNumbersAndWeekdays },
        { "leap_years_and_month_lengths", LeapYearsAndMonthLengths },
        { "invalid_dates_are_rejected", InvalidDatesAreRejected },
        { "names_are_english", NamesAreEnglish },
        { "date_text_reads_and_writes", DateTextReadsAndWrites },
        { "date_time_text_reads_and_writes", DateTimeTextReadsAndWrites },
    };
    TestRun_RunSuite(run, "GameCalendar", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
