# GameCalendar

`include/GameCalendar.h`, `source/GameCalendar.c`. Tests: `tests/GameCalendarTests.c`.

The game's calendar: pure, stateless functions over dates in the proleptic Gregorian calendar (the Gregorian rules
extended back to year 1). Built in T23 together with the game clock ([GameClock.md](GameClock.md)).

## Day numbers

The game counts dates as **day numbers**: days since 0001-01-01, which is day 0 and a Monday, as an `int64_t`. The
calendar covers the years 1 to 9999: `GAME_CALENDAR_FIRST_DAY_NUMBER` (0) to `GAME_CALENDAR_LAST_DAY_NUMBER`
(3652058, 9999-12-31). Some anchors: 1970-01-01 is 719162, 2000-01-01 is 730119, 2026-10-18 is 739906.

Day numbers are what the game stores and counts with: the clock's `day`, the world environment's `date`, the property
tables' `Date` type ([PropertyDescriptors.md](PropertyDescriptors.md)), later the scheduler and the moon. A day number
plus seconds since midnight is a full time (the clock). Adding days is integer arithmetic; only display and input go
through dates.

## Functions

- **Conversions.** `GameCalendar_TryGetDayNumber(date, &dayNumber)` (false for an invalid date) and
  `GameCalendar_GetDate(dayNumber)` (a day outside the calendar is clamped to its first or last day). Howard Hinnant's
  days-from-civil and civil-from-days algorithms (public domain): constant time, no loops over years.
- **Rules.** `GameCalendar_IsLeapYear` (every 4 years, not centuries, except every 400), `GameCalendar_GetDaysInMonth`
  (0 for a month outside 1 to 12), `GameCalendar_IsDateValid` (years 1 to 9999, an existing day: no 2026-02-29),
  `GameCalendar_IsDayNumberValid`.
- **Derived values.** `GameCalendar_GetWeekday` (`GameWeekday`, **Monday = 0** as in ISO 8601; WRDateTime's
  `DayOfWeek`, for the wall clock, starts on Sunday instead) and `GameCalendar_GetDayOfYear` (1 to 366).
- **Names.** `GameCalendar_GetMonthName` ("October"), `GameCalendar_GetWeekdayName` ("Sunday") and
  `GameCalendar_GetWeekdayShortName` ("Sun"): English, static strings, "?" for invalid input. Translation comes later
  through translate component keys (`calendar.month.<1-12>`, `calendar.weekday.<0-6>` and `.short`) with these names
  as the fallbacks: a `// TODO:` in `source/GameCalendar.c`.
- **Date text.** `GameCalendar_FormatDate(dayNumber, buffer)` appends "yyyy-mm-dd" (one growing string, formatted on
  the stack, no allocation; an invalid day number is an `ArgumentOutOfRange` error) and
  `GameCalendar_TryParseDate(text, byteCount, &dayNumber)` reads exactly that form: four, two and two ASCII digits and
  two dashes, nothing before or after, and the date must exist. This is the form files use (the `Date` property type
  in JSON).

## Date-times

A **date-time** is one moment of the calendar, for stored values (object data's `date_time`, [ObjectData.md](ObjectData.md);
later saves and script values): an `int64_t` count of **microseconds since 0001-01-01T00:00:00**, from 0 to
`GAME_CALENDAR_LAST_DATE_TIME` (the last microsecond of 9999-12-31). Its day number is the count divided by
`GAME_CALENDAR_MICROSECONDS_PER_DAY`. Integers keep it exact (equality, ordering, adding durations); the game clock's
day plus double seconds converts to it by rounding to the microsecond (added with T24).

- `GameCalendar_IsDateTimeValid(dateTime)`.
- `GameCalendar_FormatDateTime(dateTime, buffer)` appends `"yyyy-mm-ddThh:mm:ss"` (e.g. `"2026-10-18T09:30:00"`), and
  `"."` with up to six digits of fractional seconds when there are any, trailing zeros left out (`.25`). One growing
  string, formatted on the stack.
- `GameCalendar_TryParseDateTime(text, byteCount, &dateTime)` reads exactly that form (an uppercase `T`, hours 00 to 23,
  minutes and seconds 00 to 59, one to six fractional digits after a `.`), and nothing else: no time zone, no bare date.

## Custom calendars

Fantasy calendars (other months, other month or year lengths) are a possible later option, perhaps as a `calendar`
asset. **Every calendar rule lives in this module**, so such a calendar would replace these functions and nothing
else: the clock, the property tables, the moon and the scheduler only count day numbers and ask this module what a
day number means. Keep it that way: no month lengths, leap rules or date formats anywhere else.

## Thread safety

Stateless; every function is thread-safe.
