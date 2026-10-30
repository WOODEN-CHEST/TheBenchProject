# Weather (the climate asset and the weather system)

`include/Climate.h`, `source/Climate.c` (the data and its reader), `source/ClimateDefinition.c` (the asset type);
`include/WeatherSystem.h`, `source/WeatherSystem.c`. Tests: `tests/ClimateTests.c`, `tests/WeatherSystemTests.c`. The
renderer's side is `source/WorldWeatherLight.{h,c}` (private; [WorldRuntime.md](WorldRuntime.md), "Weather", tested in
`tests/WorldWeatherLightTests.c`).

The weather is **continuous values, never hard-coded states**: cloud coverage, type and density, precipitation, wetness,
the wind's direction, speed and gusts, a ground fog's density and height, and the temperature, stored in the world's
environment ([WorldData.md](WorldData.md), "Environment": its "Weather" rows), where the renderer reads them. Over game
time the weather system moves them: clouds gather, turn into rain clouds, it rains, thunders, rains less, clears. How
much of that happens is the story creator's: no weather at all, one permanent state, only what the story's scripts set,
or the simulation, with seasonal and time-of-day rules from the calendar.

## The climate asset (`Climate.h`, the `climate` type)

The fifteenth standard asset type (JSON in `climates/`, overridable by packs; loaded with `AssetManager_LoadClimate`; the
format is in `references/asset_structure.md`, "Climates"). A world names its climate in its environment (`climate`,
default `default`), so a town and a forest can differ. The shipped `climates/default.json` has seven presets in the
sequence clear, cloudy, overcast, light rain or rain, storm, light rain, cloudy, clear, and a morning fog more likely in
autumn; storms come in the afternoon and more in summer.

- **Presets** are only targets: each sets some of the quantities (`WeatherQuantity`: a number, or a `[min, max]` range
  drawn on every visit), how long it lasts, and weighted links to the presets that may follow. Values a preset leaves
  out keep moving toward their previous targets. A preset may also give month weights (the seasons), the hours it may
  start in (fog only at dawn; a window past midnight wraps), how long the move into it takes (every value it sets then
  arrives at its target at the end, all together), its own rates, its gusts and its lightning.
- **The root's settings**: the mode, the seed (a number, or `per_save`), the first preset (or `random`), an overall
  speed, the global rates, how fast surfaces get wet and dry, the clouds' ranges over which rain and lightning become
  possible, the forecast's accuracy, and the fixed mode's preset or values.
- **The reader** (`Climate_ReadJSON`) treats the file as hostile: unknown keys are refused everywhere, every value is
  checked against its rule while it is read (names exist, weights above 0, exactly twelve month weights, hours in [0, 24],
  ranges in order, values in their quantity's range: the same bounds as the environment's rows, which a test pins), and
  every message names the path to the bad value (`Climate "default": presets[5] ("storm").lightning.per_minute: ...`).
  Presets are read in two passes, names first, so a link may name a preset that comes later.
- **The data** is one block of plain data with no pointers inside (links are indices; at most `CLIMATE_MAX_PRESETS` 64
  presets and `CLIMATE_MAX_LINKS` 512 links), so the loaded asset is a copy of the definition's and no dependency is
  held. Read it through `Climate_GetSettings`, `Climate_GetPreset`, `Climate_GetLinks` and `Climate_TryFindPreset`.

## The weather system (`WeatherSystem.h`)

Game logic, headless. `WeatherSystem_Construct(system, assets, logger)` mints the system's own asset user (it holds its
climate); `WeatherSystem_Deconstruct` retires it. Who owns one today: the world test frame (below); in the game it
belongs to the game state, saved with it.

### Each tick

`WeatherSystem_Update(system, environment, clock, gameSeconds, realSeconds)`, after the clock moved
([GameClock.md](GameClock.md)): `gameSeconds` is what the clock moved (`GameClockCrossings.GameSeconds`, a fast-forward's
too), `realSeconds` the real time of play (0 while the game is paused or skips time).

- **The climate**: the one the environment names is loaded when it is new or changed; one that fails is logged once and
  not retried until the name changes, and the weather then stays as it is (a flash it lit goes out). The first update
  starts the system: the seed (a new one drawn per game for `per_save`), the mode and speed of the climate, the first
  preset; the environment's values are where the weather starts, and every target starts at them (a quantity no preset
  sets stays put). In the scripted mode nothing is moved at the start, or when another climate takes over: the first
  preset only gives its gusts and lightning.
- **The game-time weather** (unless locked) runs `gameSeconds` x the speed, in steps of at most 0.05 weather hours that
  end where a preset ends. So a fast-forward (sleeping eight hours in one update) passes through every preset it should,
  and a forecast with other steps chooses the same presets at the same times (a preset lasts at least 0.01 weather
  hours, so the steps are bounded by the time). In each step:
  - in the simulated mode, when the preset's time is up the next one is chosen from its links by weight x the month's
    weight of each, among those whose start hours allow the hour (none allowed: the preset stays for another visit); its
    targets and duration are drawn;
  - each value moves toward its target at its rate (an angle along the shorter way round); rain heads for no more than
    the clouds allow (`rain_cloud_range` over coverage x density: no rain from thin clouds, even under a rainy target),
    so it never rises past them and eases off at its own rate when they thin;
  - wetness rises at `wetting_per_hour` x the rain and dries at `drying_per_hour` without it, up to three times faster in
    full sun under a clear sky and once more for every 10 m/s of wind (the sun from the environment's own model at the
    step's middle: `WorldEnvironment_GetDaylightFactorAt`); the rain is taken at the mean of the step's ends (the
    trapezoid rule), so long steps wet as many short ones do.
- **The real-time weather** runs `realSeconds`, locked or not:
  - **gusts**: a smooth (smoothstep) rise to a peak between half the preset's strength and all of it, then a fall to a
    lull below a fifth of it, each half lasting about half a gust's period (`per_minute`, spread exponentially, at least
    1.5 s): the wind is seen moving things at its real speed, so its gusts are real seconds too;
  - **lightning**: a Poisson process at the preset's rate per real minute and per game hour (`per_hour` counts the
    weather's own hours) times how storm-like the clouds are (`storm_cloud_range` over type x density), at most one strike
    an update. A strike sets the flash to 1, which decays with a time constant of 0.075 real seconds (from 1 to under 2 %
    in 0.3 s) and up to three return strokes 0.04 to 0.12 s apart light again (all drawn with the strike, so nothing
    random waits on them); it has a level direction and a distance
    spread evenly over the area between the preset's distances (so most are far), and queues a `WeatherStrike` (its game
    date and time, direction, distance: its thunder arrives distance / `WEATHER_SYSTEM_SPEED_OF_SOUND` real seconds
    after the flash). The game reads the queue (`WeatherSystem_GetStrikes`) and clears it (`_ClearStrikes`); it holds at
    most 64, dropping the oldest.
- The values, the flash, the strike's direction and distance are written into the environment. The system logs the
  climate's start, every preset change (at most one line an update), mode and lock changes and failures; never per tick.

**Why two clocks.** The climate is a story's weather: presets, rates, wetting and drying count game hours, so the
weather keeps pace with the clock and sleeping moves it. Gusts, the flash and the thunder are things the eye and the ear
take in: at a 72x clock a gust per game minute would flicker. The real-time stream has its own random stream, so the
presets never depend on the frame rate.

### Modes

| Mode | What the system does |
|---|---|
| `simulated` (the default) | The presets follow each other by their links and the calendar's rules. |
| `fixed` | Holds the climate's `fixed_preset` or `fixed_values` (what they leave out stays where it is), else the active preset, for good: the values move to it and stay. Wetness, gusts and lightning still follow the values (a permanent rain still wets the streets). |
| `scripted` | No preset follows another by itself; values move only toward targets the API set. Wetness, gusts and lightning still follow. |
| `off` | Touches nothing: the environment's values stay as they are (a world file's, as authored). A flash it lit goes out at the next update, however the mode was entered (its one last write). |

### The API (for stories; script commands come with the scripting language)

`WeatherSystem_SetMode`; `WeatherSystem_SetPreset(system, name, transitionHours)` (every value the preset sets arrives
after that many weather hours, 0 at the next update; in the simulated mode the simulation then runs from it);
`WeatherSystem_SkipPreset` (the next one now, chosen as when its time is up); `WeatherSystem_SetValuesNow(system,
environment, mask, values)` (the values and their targets at once: they stay until a preset or a script moves them);
`WeatherSystem_SetLocked` (holds the game-time weather for a scene; gusts and lightning go on);
`WeatherSystem_SetSeed`, `WeatherSystem_SetSpeed`; `WeatherSystem_SetClimate` (writes the name into the environment: the
next update loads it and takes over with its mode, speed and first preset, from the weather as it is);
`WeatherSystem_Strike` (a strike now). Refused input changes nothing and returns `ArgumentOutOfRange`,
`InvalidOperation` (an unknown preset) or `InvalidState` (before the system started; `SetMode`, `SetPreset`,
`SkipPreset` and the forecast also after a read, until the next update finds the saved preset).

### The forecast

`WeatherSystem_Forecast(system, environment, clock, hoursAhead, stepHours, destination)` runs a copy of the simulation
ahead (at most a year of the weather's own hours: the hours ahead x the speed) and appends a `WeatherForecastStep` per
step: the date and time, the values and the preset it reports. At the
climate's `forecast_accuracy` 1 it is what then happens (unless a script steps in). Below 1, some later visits are
reported as one of their links, by the links' weights: a share of 1 - accuracy, growing toward it over about a day of
lead (none for the visit going on now); each visit's report is drawn from the seed and the visit's start, so the same
forecast is given every time it is asked, and a visit is reported right once it starts. Gusts and lightning are not
forecast. For a forecast on the radio, the television or in the newspaper.

### Saving

Everything that decides what comes next is in the system's property table (`WeatherSystem_GetPropertyTable`, table
`weather_system`; [PropertyDescriptors.md](PropertyDescriptors.md)): the climate's and the active preset's names, whether
it started, the seed, the mode, the lock, the speed, the preset's hours left and start, every quantity's target and rate,
the pending rates of an API call, both random streams' state and the gusts' state. Save it with the table's writers
on the system itself; after a read (or any `Properties_Set`) the next update finds the climate and the preset by name
(a preset the climate no longer has restarts the climate's first) and the simulation goes on exactly as if it had never
stopped (the tests check it bit for bit, saved at the start and in a strike's flicker too). The flash, the pending
return strokes and the queued strikes are not saved.
The weather's values themselves are the environment's, saved with the world.

## The test frame's weather (`source/WorldTestWeather.{h,c}`)

The world test frame runs the climate its test world names (the default climate) on its clock: after the clock moves
each tick, with the clock's game seconds and the tick's real seconds (0 while the clock is paused). Its keys (the F
keys are all taken): **Insert** moves into the climate's next preset in the file's order (over a quarter of a weather
hour), **Home** locks and unlocks, **End** steps the speed (x1, x10, x100 the climate's), **Page Up** strikes now,
**Page Down** steps the mode (simulated, fixed, scripted, off; off gives the world file's weather back), and the
**backslash** logs a forecast of the next 24 game hours (a `*` marks a preset the forecast gets wrong). Every strike is
logged with its distance and direction, and the I key's status adds the weather's line.

## Known limits

- **No clouds are drawn yet**: the renderer shows the clouds as one even deck over the sky ([WorldRuntime.md](WorldRuntime.md),
  "Weather"); a camera above the clouds' height still sees it.
- **Wetness is global**: covered surfaces get wet too (the rain's own task keeps them dry through the sky's visibility).
- **Temperature** moves with the presets but nothing reads it yet (snow, breath).
- **Fixed values and the rain**: the clouds' rule holds in every mode, so a fixed rain needs dense clouds too.
- **The air's haze** (`sky_turbidity`, `haze_height`) is the world's; presets do not move it.
- **Extreme speeds**: a step of 0.05 weather hours costs about 0.27 microseconds, so a long skip at a high speed takes
  time (an eight-hour sleep at 10000 times the speed about 0.4 s), and a preset change is logged each time (at the
  test frame's x100 nearly every tick in a storm).
