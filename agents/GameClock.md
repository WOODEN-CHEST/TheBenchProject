# GameClock

`include/GameClock.h`, `source/GameClock.c`. Tests: `tests/GameClockTests.c`.

The game's one clock: the in-game date and time that everything reads. Built in T23 (architecture proposal 3.3, "one
game clock"; decision G9: a calendar date that matters in gameplay). Dates come from [GameCalendar.md](GameCalendar.md).

## State and settings

| Field | Key | Type | Default | Rule |
|---|---|---|---|---|
| `_day` | `day` | Date (day number) | 2026-10-18 (`GAME_CLOCK_DEFAULT_DAY_NUMBER`) | inside the calendar |
| `_secondsOfDay` | `seconds_of_day` | Double | 43200 (noon) | finite, [0, 86400) |
| `IsPaused` | `is_paused` | Bool | false | |
| `_timeScale` | `time_scale` | Double | 72 (a 20-minute day) | finite, > 0 |
| `_maxStepSeconds` | `max_step_seconds` | Double | 3600 (one game hour) | finite, > 0 |

- **Whole days are integers and the seconds a double,** so precision never degrades over a long game.
- **No drift.** Each step is added to the seconds with Kahan's compensated summation; the rounding error of the last
  step is kept in the runtime-only `_secondsCarry` (a few picoseconds, not in the table, not saved) and taken off the
  next one. In the test (1000 game days of 480 Hz ticks at 1.2 game seconds each) the clock ends 3 nanoseconds from the
  exact time, which is the binary rounding of the step itself; plain summation would end 0.12 ms off there, 0.36 ms at
  the default speed and more at slower speeds. Jumps and every change made through the table (the table's `AfterSet`
  hook) reset the carry.
- The `_` fields are read-only: getters (`GameClock_GetDay`, `GetSecondsOfDay`, `GetTimeScale`, `GetMaxStepSeconds`,
  static inline) and validating setters. `IsPaused` is a plain public field.
- **The property table** (`GameClock_GetPropertyTable`, table `game_clock`, every row `PROPERTY_FLAGS_SETTING`): the
  clock saves with the game state (JSON `"day": "2026-10-18"`, GHDF the day number), and the inspector and scripts reach
  it by key. The setters check their values with `Properties_CheckValue` against the same rows, so the rules live once.

## Moving the clock

- **`GameClock_Advance(clock, realSeconds, &crossings)`, once per update tick** with `ProgramTime.PassedTime`: moves
  by `realSeconds × TimeScale`, at most `MaxStepSeconds` (a guard against a huge leap after a stall, which also catches
  an overflow), unless paused. Bad input (NaN, negative, zero) does nothing. Constant time, no allocation.
- **`GameClock_AddGameSeconds(clock, gameSeconds, &crossings)`**: a fast-forward (sleeping, skipping a scene), also
  while paused. Finite and >= 0; refused (unchanged) if it would pass 9999-12-31.
- **Jumps:** `GameClock_SetDateTime(clock, year, month, day, hour, minute, second)` (validated calendar fields) and
  `GameClock_SetDayAndTime(clock, dayNumber, secondsOfDay)` (the precise form, for tools and for starting the clock from
  saved or authored data). Nothing changes on failure. **A jump reports no crossings**: whoever jumps decides what the
  skipped time means (a scheduler might fire skipped events, or not).
- **The calendar's end:** the clock stops at the last instant of 9999-12-31 (the last double before 86400 seconds);
  `Advance` stays there.

### Crossings

`GameClockCrossings` reports what a move (Advance or AddGameSeconds) passed:
- `GameSeconds`: the game seconds the clock moved, 0 when it did not (paused, bad input, at the end);
- `Midnights`: how many times the date changed;
- `Hours`: the whole hours passed, midnights included.

A move passes every boundary after its start, up to and including its end: 08:30 onto 09:00 passes 09:00, and moving
on from 09:00 does not pass it again. So per-tick crossings add up to exactly the boundaries crossed over many ticks.
The scheduler of the game-core round (events at dates and times, architecture A4) will wake on these.

## Reading the time

- `GameClock_GetDate` (`GameDate`), `GameClock_GetWeekday`, `GameClock_GetHourMinuteSecond` (whole seconds, the
  fraction cut off, never rounded up into the next second or day).
- `GameClock_GetTimeOfDay`: a float fraction for the sky (0 midnight, 0.5 noon; 1 only by rounding just before
  midnight).
- `GameClock_GetTotalDays`: `Day + SecondsOfDay / 86400` as a double, for the moon's phase and the stars' yearly drift.
- `GameClock_FormatDebug`: "Sun 2026-10-18 09:30:00" for logs (appends, formatted on the stack).

## Who reads the clock (the rule for later tasks)

**Every user of game time takes it from this clock; nothing keeps its own time.**
- **The sky.** `WorldEnvironment` no longer advances time ([WorldData.md](WorldData.md), "Environment"): its
  `time_of_day` and `date` are what the sky shows. Whoever runs the game writes them from the clock every tick
  (`TimeOfDay = GameClock_GetTimeOfDay`, `Date = GameClock_GetDay`); in a world file they are the sky's starting
  point. The moon's phase and position, the stars' drift and the seasons (T03) read the environment's date and time,
  i.e. the clock.
- **The weather (T16)** moves by the game seconds the clock moved: `Crossings.GameSeconds` of each Advance and each
  fast-forward (`WeatherSystem_Update(..., clock, gameSecondsElapsed)` in its task file). A paused clock pauses the
  weather; sleeping moves it.
- **Later:** the scheduler (game core, A4), NPC schedules, scripts' `wait(2h)` and `time.*` functions
  (`handoff/2026-10_scripting_language/PROPOSAL.md`), story deadlines and office hours.

Who owns the clock: today the world test frame (`WorldTestFrame`, [WorldRuntime.md](WorldRuntime.md)), which starts
it from the world environment's date and time at a 2-minute-day speed (time scale 720). In the game it will belong to
the game state (architecture 3.3), started from the story's settings (a `// TODO:` in `source/WorldTestFrame.c`
until stories have a manifest, architecture A5).

## Not here (yet)

- The scheduler (events at dates and times): the game-core round.
- Time zones and daylight saving: never planned.
- Seasons for the sun: `WorldEnvironment.AreSeasonsEnabled` (T03, off by default), which reads the environment's date
  ([WorldData.md](WorldData.md), "Environment").
- Custom calendars: see [GameCalendar.md](GameCalendar.md).

Not thread-safe.
